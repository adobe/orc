// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// stdc++
#include <array>
#include <csignal>
#include <cxxabi.h>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <numeric>
#include <set>
#include <thread>
#include <unordered_map>

// stlab
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/serial_queue.hpp>
#include <stlab/concurrency/utility.hpp>

// toml++
#include <toml++/toml.h>

// tbb
#include <tbb/concurrent_unordered_map.h>

// application
#include "orc/features.hpp"
#include "orc/orc.hpp"
#include "orc/parse_file.hpp"
#include "orc/settings.hpp"
#include "orc/str.hpp"
#include "orc/string_pool.hpp"
#include "orc/task_system.hpp"
#include "orc/tracy.hpp"
#include "orc/version.hpp"

//--------------------------------------------------------------------------------------------------

namespace {

//--------------------------------------------------------------------------------------------------

template <class Container, class T>
bool sorted_has(const Container& c, const T& x) {
    auto found = std::lower_bound(c.begin(), c.end(), x);
    return found != c.end() && *found == x;
}

//--------------------------------------------------------------------------------------------------

std::string exec(const char* cmd) {
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);

    if (!pipe) {
        throw std::runtime_error("popen() failed!");
    }

    std::array<char, 1024> buffer;
    std::string result;

    while (fgets(buffer.data(), buffer.size(), pipe.get())) {
        result += buffer.data();
    }

    return result;
}

void open_output_file(const std::string& a, const std::string& b) {
    std::filesystem::path path(b);
    if (!a.empty()) {
        path = a + '.' + b;
    }
    auto& output_file = globals::instance()._fp;
    output_file.open(path);
    if (!output_file) {
        throw std::logic_error("failed to open output file: " + path.string());
    }
}

//--------------------------------------------------------------------------------------------------

template <typename T>
T parse_enval(std::string&&) = delete;

template <>
std::string parse_enval(std::string&& x) {
    assert(!x.empty());
    return x;
}

template <>
bool parse_enval(std::string&& x) {
    assert(!x.empty());
    return x != "0" && toupper(std::move(x)) != "FALSE";
}

template <>
std::size_t parse_enval(std::string&& x) {
    assert(!x.empty());
    return std::max<int>(std::atoi(x.c_str()), 0);
}

template <typename T>
T derive_configuration(const char* key,
                       const toml::parse_result& settings,
                       T&& fallback) {
    T result = settings[key].value_or(fallback);
    std::string envar = toupper(std::string("ORC_") + key);
    if (const char* enval = std::getenv(envar.c_str())) {
        result = parse_enval<T>(enval);
    }
    return result;
}

//--------------------------------------------------------------------------------------------------

void process_orc_configuration(const char* bin_path_string) {
    std::filesystem::path pwd(std::filesystem::current_path());
    std::filesystem::path bin_path(pwd / bin_path_string);
    std::filesystem::path config_path;

    // run up the directories looking for the first instance of .orc-config or _orc-config
    while (true) {
        std::filesystem::path parent = bin_path.parent_path();

        if (parent == bin_path.root_directory()) {
            break;
        }

        std::filesystem::path candidate = parent / ".orc-config";

        if (exists(candidate)) {
            config_path = std::move(candidate);
            break;
        }

        candidate = parent / "_orc-config";

        if (exists(candidate)) {
            config_path = std::move(candidate);
            break;
        }

        bin_path = parent;
    }

    toml::parse_result settings;

    if (exists(config_path)) {
        try {
            settings = toml::parse_file(config_path.string());
        } catch (const toml::parse_error& err) {
            cerr_safe([&](auto& s) { s << "Parsing failed:\n" << err << "\n"; });
            throw std::runtime_error("configuration parsing error");
        }
    } else {
        cerr_safe([&](auto& s) { s << "ORC config file: not found\n"; });
    }

    auto& app_settings = settings::instance();

    app_settings._graceful_exit = derive_configuration("graceful_exit", settings, false);
    app_settings._max_violation_count = derive_configuration("max_error_count", settings, std::size_t(0));
    app_settings._forward_to_linker = derive_configuration("forward_to_linker", settings, true);
    app_settings._standalone_mode = derive_configuration("standalone_mode", settings, false);
    app_settings._dylib_scan_mode = derive_configuration("dylib_scan_mode", settings, false);
    app_settings._parallel_processing = derive_configuration("parallel_processing", settings, true);
    app_settings._filter_redundant = derive_configuration("filter_redundant", settings, true);
    app_settings._print_object_file_list = derive_configuration("print_object_file_list", settings, false);
    app_settings._relative_output_file = derive_configuration("relative_output_file", settings, std::string());

    const std::string log_level = derive_configuration("log_level", settings, std::string("warning"));
    const std::string output_file = derive_configuration("output_file", settings, std::string());
    const std::string output_file_mode = derive_configuration("output_file_mode", settings, std::string("text"));

    // Do this early so we can log the ensuing output if it happens.
    if (!output_file.empty()) {
        open_output_file(std::string(), output_file);
    }

    // Do this early, too, so we can _suppress_ the output if we need to.
    if (output_file_mode == "text") {
        app_settings._output_file_mode = settings::output_file_mode::text;
    } else if (output_file_mode == "json") {
        app_settings._output_file_mode = settings::output_file_mode::json;
    } else if (log_level_at_least(settings::log_level::warning)) {
        cout_safe([&](auto& s) {
            s << "warning: unknown output_file_mode '" << output_file_mode << "'; using text\n";
        });
    }

    if (log_level == "silent") {
        app_settings._log_level = settings::log_level::silent;
    } else if (log_level == "warning") {
        app_settings._log_level = settings::log_level::warning;
    } else if (log_level == "info") {
        app_settings._log_level = settings::log_level::info;
    } else if (log_level == "verbose") {
        app_settings._log_level = settings::log_level::verbose;
    } else {
        // not a known value. Switch to verbose!
        app_settings._log_level = settings::log_level::verbose;
        cout_safe(
            [&](auto& s) { s << "warning: unknown log_level '" << log_level << "'; using verbose\n"; });
    }

    if (app_settings._standalone_mode && app_settings._dylib_scan_mode) {
        throw std::logic_error(
            "Both standalone and dylib scanning mode are enabled. Pick one.");
    }

    if (app_settings._dylib_scan_mode) {
        app_settings._forward_to_linker = false;
    }

    auto read_string_list = [&_settings = settings](const char* name) {
        std::vector<std::string> result;
        if (auto* array = _settings.get_as<toml::array>(name)) {
            for (const auto& entry : *array) {
                if (auto symbol = entry.value<std::string>()) {
                    result.push_back(*symbol);
                }
            }
        }
        std::sort(result.begin(), result.end());
        return result;
    };

    app_settings._symbol_ignore = read_string_list("symbol_ignore");
    app_settings._violation_report = read_string_list("violation_report");
    app_settings._violation_ignore = read_string_list("violation_ignore");

    if (!app_settings._violation_report.empty() &&
        !app_settings._violation_ignore.empty()) {
        if (log_level_at_least(settings::log_level::warning)) {
            cout_safe([&](auto& s) {
                s << "warning: Both `violation_report` and `violation_ignore` lists found\n";
                s << "warning: `violation_report` will be ignored in favor of `violation_ignore`\n";
            });
        }
    }

    if (log_level_at_least(settings::log_level::info)) {
        cout_safe([&](auto& s) {
            s << "info: ORC config file: " << config_path.string() << "\n";
        });
    }
}

//--------------------------------------------------------------------------------------------------

auto derive_filelist_file_list(const std::filesystem::path& filelist) {
    std::vector<std::filesystem::path> result;
    std::ifstream input(filelist, std::ios::binary);

    if (!input) throw std::runtime_error("problem opening filelist for reading");

    static constexpr auto buffer_sz{1024};
    std::array<char, buffer_sz> buffer;

    // The link file list contains object files, one per line.
    while (input) {
        input.getline(&buffer[0], buffer_sz);
        if (strnlen(&buffer[0], 1024)) result.push_back(&buffer[0]);
    }

    return result;
}

//--------------------------------------------------------------------------------------------------

auto find_artifact(std::string_view type,
                   const std::vector<std::filesystem::path>& directories,
                   std::string_view artifact) {
    for (const auto& dir : directories) {
        std::filesystem::path candidate(dir / artifact);
        if (!exists(candidate)) continue;
        return canonical(candidate); // canonical requires the path exists
    }

    if (log_level_at_least(settings::log_level::warning)) {
        cout_safe(
            [&](auto& s) { s << "warning: Could not find " << type << " '" << artifact << "'\n"; });
    }

    return std::filesystem::path();
}

//--------------------------------------------------------------------------------------------------

struct cmdline_results {
    std::vector<std::filesystem::path> _file_object_list;
    bool _ld_mode{false};
    bool _libtool_mode{false};
};

//--------------------------------------------------------------------------------------------------

namespace {

//--------------------------------------------------------------------------------------------------

bool direct_input_file(const std::string_view& p) {
    // .o: object file
    // .a: ar file (presumably containing .o files)
    // .dwarf: flattened dSYM file (see https://llvm.org/docs/CommandGuide/dsymutil.html)
    // .dSYM: dSYM folder.
    return p.ends_with(".o") || p.ends_with(".a") || p.ends_with(".dwarf") || p.ends_with(".dSYM");
}

//--------------------------------------------------------------------------------------------------

} // namespace

//--------------------------------------------------------------------------------------------------

cmdline_results process_command_line(int argc, char** argv) {
    cmdline_results result;

    if (log_level_at_least(settings::log_level::verbose)) {
        cout_safe([&](auto& s) {
            s << "verbose: arguments:\n";
            for (std::size_t i{0}; i < argc; ++i) {
                s << "  " << argv[i] << '\n';
            }
        });
    }

    if (settings::instance()._standalone_mode || settings::instance()._dylib_scan_mode) {
        for (std::size_t i{1}; i < argc; ++i) {
            result._file_object_list.push_back(argv[i]);
        }

        if (settings::instance()._dylib_scan_mode &&
            result._file_object_list.size() != 1 &&
            log_level_at_least(settings::log_level::warning)) {
            cout_safe([&](auto& s) {
                s << "warning: dylib scanning with more than one top-level artifact may yield false positives.\n";
            });
        }
    } else {
        std::vector<std::filesystem::path> library_search_paths;
        std::vector<std::filesystem::path> framework_search_paths;

        std::vector<std::string> unresolved_libraries;
        std::vector<std::string> unresolved_frameworks;

        for (std::size_t i{1}; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "-o" || arg == "--output") {
                std::string filename(argv[++i]);

                if (!settings::instance()._relative_output_file.empty()) {
                    open_output_file(filename, settings::instance()._relative_output_file);
                }

                // next argument is the output file. Use its name to decide
                // whether we should run in ld or libtool mode. If the mode
                // has already been detected, we can skip this step.
                if (!result._libtool_mode && !result._ld_mode) {
                    if (filename.ends_with(".a")) {
                        result._libtool_mode = true;
                        if (log_level_at_least(settings::log_level::verbose)) {
                            cout_safe(
                                [&](auto& s) { s << "verbose: mode: libtool (by filename)\n"; });
                        }
                    } else {
                        result._ld_mode = true;
                        if (log_level_at_least(settings::log_level::verbose)) {
                            cout_safe([&](auto& s) { s << "verbose: mode: ld (by filename)\n"; });
                        }
                    }
                }
            } else if (arg == "-Xlinker") {
                // next argument is a linker specific flag which we don't need.
                ++i;
            } else if (arg == "-object_path_lto") {
                // next argument is an object file we don't need.
                ++i;
            } else if (arg == "-static") {
                result._libtool_mode = true;
                assert(!result._ld_mode);
                if (log_level_at_least(settings::log_level::verbose)) {
                    cout_safe([&](auto& s) { s << "verbose: mode: libtool (static)\n"; });
                }
            } else if (arg == "-target") {
                result._ld_mode = true;
                assert(!result._libtool_mode);
                if (log_level_at_least(settings::log_level::verbose)) {
                    cout_safe([&](auto& s) { s << "verbose: mode: ld (target)\n"; });
                }
            } else if (arg == "-lc++") {
                // ignore the C++ standard library
            } else if (arg == "-lSystem") {
                // ignore the macOS System library
            } else if (arg == "-lto_library") {
                // ignore this system library
            } else if (arg.starts_with("-filelist")) {
                for (auto& entry : derive_filelist_file_list(argv[++i])) {
                    result._file_object_list.push_back(std::move(entry));
                }
            } else if (arg.starts_with("-L")) {
                library_search_paths.push_back(arg.substr(2));
            } else if (arg.starts_with("-l")) {
                auto library = std::string("lib") + arg.substr(2).data() + ".a";
                unresolved_libraries.push_back(std::move(library));
            } else if (arg.starts_with("-F")) {
                framework_search_paths.push_back(arg.substr(2));
            } else if (arg.starts_with("-framework")) {
                auto framework = std::string(argv[++i]);
                framework = framework + ".framework/" + framework;
                unresolved_frameworks.push_back(std::move(framework));
            } else if (direct_input_file(arg)) {
                result._file_object_list.push_back(arg);
            }
        }

        // Now that we have all the library/framework paths, go looking for libraries/frameworks.
        for (const auto& library : unresolved_libraries) {
            auto library_path = find_artifact("library", library_search_paths, library);
            if (exists(library_path)) {
                result._file_object_list.push_back(std::move(library_path));
            }
        }

        for (const auto& framework : unresolved_frameworks) {
            // REVISIT: (fosterbrereton) Should we elide all system frameworks?
            if (framework == "Foundation" || framework == "CoreFoundation") {
                continue; // excluded
            } else {
                auto artifact_path = find_artifact("framework", framework_search_paths, framework);
                if (exists(artifact_path)) {
                    result._file_object_list.push_back(std::move(artifact_path));
                }
            }
        }
    }

    return result;
}

//--------------------------------------------------------------------------------------------------

auto epilogue(bool exception) {
    const auto& g = globals::instance();

    if (g._object_file_count == 0) {
        if (settings::instance()._output_file_mode == settings::output_file_mode::json) {
            cout_safe([](auto& s){
                s << orc::version_json() << '\n';
            });
        } else {
            cout_safe([&](auto& s) {
                const auto local_build = ORC_VERSION_STR() == std::string("local");
                const std::string tag_url = local_build ? "" : std::string(" (https://github.com/adobe/orc/releases/tag/") + ORC_VERSION_STR() + ")";
                s << "ORC (https://github.com/adobe/orc)\n";
                s << "    version: " << ORC_VERSION_STR() << tag_url << '\n';
                s << "    sha: " << ORC_SHA_STR() << '\n';
            });
        }
    } else if (log_level_at_least(settings::log_level::warning)) {
        // Make sure these values are in sync with the `synopsis` json blob in `to_json`.
        cout_safe([&](auto& s) {
            s << "ORC complete.\n"
              << "  " << g._odrv_count << " ODRV(s) reported\n"
              << "  " << g._object_file_count << " object file(s) processed\n"
              << "  " << g._die_processed_count << " dies processed\n"
              << "  " << g._die_skipped_count << " dies skipped ("
              << format_pct(g._die_skipped_count, g._die_processed_count) << ")\n"
              << "  " << g._unique_symbol_count << " unique symbols\n";
        });
    }

    if (exception) {
        return EXIT_FAILURE;
    } else if (settings::instance()._graceful_exit) {
        return EXIT_SUCCESS;
    }

    return g._odrv_count == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}

//--------------------------------------------------------------------------------------------------

auto interrupt_callback_handler(int signum) {
    (void)epilogue(true); // let's consider an interrupt an exceptional situation.
    std::exit(signum);
}

//--------------------------------------------------------------------------------------------------

void maybe_forward_to_linker(int argc, char** argv, const cmdline_results& cmdline) {
    if (!settings::instance()._forward_to_linker) return;

    std::filesystem::path executable_path =
        rstrip(exec("xcode-select -p")) + "/Toolchains/XcodeDefault.xctoolchain/usr/bin/";

    if (cmdline._ld_mode) {
        if (settings::instance()._standalone_mode) {
            // If we're in standalone mode, most likely the user has copypasta'd
            // a set of command line arguments that are going to clang++ out of Xcode.
            // In that case we'll call clang, which will call ld under the hood.
            // (in this case we may want to filter out the `-alt-ld` flag in the args.)
            executable_path /= "clang++";
        } else {
            // If we're not in standalone mode, then the command line arguments we're getting
            // have been processed by clang already, and we need to call ld directly.
            executable_path /= "ld";
        }
    } else if (cmdline._libtool_mode) {
        executable_path /= "libtool";
    } else {
        if (log_level_at_least(settings::log_level::verbose)) {
            cout_safe([&](auto& s) {
                s << "verbose: libtool/ld mode could not be derived; forwarding to linker disabled\n";
            });
        }

        return;
    }

    if (!exists(executable_path)) {
        throw std::runtime_error("Could not forward to linker: " + executable_path.string());
    } else if (log_level_at_least(settings::log_level::verbose)) {
        cout_safe(
            [&](auto& s) { s << "verbose: forwarding to " + executable_path.string() + "\n"; });
    }

    std::string arguments = executable_path.string();
    for (std::size_t i{1}; i < argc; ++i) {
        // Gotta re-add the escape characters into our params that have spaces in them.
        arguments += std::string(" ") + join(split(argv[i], " "), "\\ ");
    }

    // REVISIT: (fbrereto) We may need to capture/forward stderr here as well.
    // Also, if the execution of the linker ended in a failure, we need to bubble
    // that up immediately, and preempt ORC processing.
    cout_safe([&](auto& s) { s << exec(arguments.c_str()); });
}

//--------------------------------------------------------------------------------------------------

} // namespace

//--------------------------------------------------------------------------------------------------

int main(int argc, char** argv) try {
    orc::profiler::initialize();

    signal(SIGINT, interrupt_callback_handler);

    process_orc_configuration(argv[0]);

    cmdline_results cmdline = process_command_line(argc, argv);

    if (settings::instance()._print_object_file_list) {
        for (const auto& input_path : cmdline._file_object_list) {
            cout_safe([&](auto& s) { s << input_path.string() << '\n'; });
        }

        return EXIT_SUCCESS;
    }

    maybe_forward_to_linker(argc, argv, cmdline);

    if (cmdline._file_object_list.empty()) {
        return epilogue(false);
    }

    std::vector<odrv_report> reports = orc_process(std::move(cmdline._file_object_list));
    std::vector<odrv_report> violations;
    std::vector<std::string> filtered_categories;
    const auto& settings = settings::instance();
    auto& globals = globals::instance();
    const auto max_odrv_count = settings._max_violation_count;
    const bool json_mode = settings._output_file_mode == settings::output_file_mode::json;

    for (const auto& report : reports) {
        if (!emit_report(report)) {
            filtered_categories.push_back(report.filtered_categories());
            continue;
        }

        violations.push_back(report);

        // Administrivia
        ++globals._odrv_count;

        if (max_odrv_count > 0 && globals._odrv_count >= max_odrv_count) {
            if (log_level_at_least(settings::log_level::warning)) {
                cout_safe([&](auto& s) { s << "warning: ODRV limit reached\n"; });
            }
            break;
        }
    }

    assert(globals._odrv_count == violations.size());

    if (json_mode && globals._fp.is_open()) {
        globals._fp << orc::to_json(violations);
    }

    for (const auto& report : violations) {
        cout_safe([&](auto& s) {
            s << report; // important to NOT add the '\n', because lots of reports are empty, and it
                         // creates a lot of blank lines
        });
    }

    return epilogue(false);
} catch (const std::exception& error) {
    cerr_safe([&](auto& s) { s << "Fatal error: " << error.what() << '\n'; });
    return epilogue(true);
} catch (...) {
    cerr_safe([&](auto& s) { s << "Fatal error: unknown\n"; });
    return epilogue(true);
}

//--------------------------------------------------------------------------------------------------
