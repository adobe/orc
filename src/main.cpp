// Copyright 2021 Adobe
// All Rights Reserved.
//
// NOTICE: Adobe permits you to use, modify, and distribute this file in accordance with the terms
// of the Adobe license agreement accompanying it.

// stdc++
#include <array>
#include <cxxabi.h>
#include <filesystem>
#include <fstream>
#include <list>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <set>

// stlab
#include <stlab/concurrency/default_executor.hpp>
#include <stlab/concurrency/future.hpp>
#include <stlab/concurrency/utility.hpp>
#include <stlab/concurrency/serial_queue.hpp>

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

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

auto& ostream_safe_mutex() {
    static std::mutex m;
    return m;
}

template <class F>
void ostream_safe(std::ostream& s, F&& f) {
    std::lock_guard<std::mutex> lock{ostream_safe_mutex()};
    std::forward<F>(f)(s);
    if (globals::instance()._fp.is_open()) {
        std::forward<F>(f)(globals::instance()._fp);
    }
}

template <class F>
void cout_safe(F&& f) {
    ostream_safe(std::cout, std::forward<F>(f));
}

/**************************************************************************************************/

template <class Container, class T>
bool sorted_has(const Container& c, const T& x) {
    auto found = std::lower_bound(c.begin(), c.end(), x);
    return found != c.end() && *found == x;
}

/**************************************************************************************************/

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

/**************************************************************************************************/

void process_orc_config_file(const char* bin_path_string) {
    // The calls to std::cout in this routine don't need to be threadsafe (too early)

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

    if (exists(config_path)) {
        try {
            auto settings = toml::parse_file(config_path.string());
            auto& app_settings = settings::instance();

            app_settings._graceful_exit = settings["graceful_exit"].value_or(false);
            app_settings._max_violation_count = settings["max_error_count"].value_or(0);
            app_settings._forward_to_linker = settings["forward_to_linker"].value_or(true);
            app_settings._print_symbol_paths = settings["print_symbol_paths"].value_or(false);
            app_settings._standalone_mode = settings["standalone_mode"].value_or(false);
            app_settings._parallel_processing = settings["parallel_processing"].value_or(true);
            app_settings._show_progress = settings["show_progress"].value_or(false);
            app_settings._filter_redundant = settings["filter_redundant"].value_or(true);
            app_settings._print_object_file_list =
                settings["print_object_file_list"].value_or(false);

            if (settings["output_file"]) {
                try {
                    std::string fn = *settings["output_file"].value<std::string>();
                    globals::instance()._fp.open(fn);
                }
                catch (const std::exception& e) {
                    std::cout << "warning: Could not open output file: " << settings["output_file"] << "\n";
                }
            }

            if (auto log_level = settings["log_level"].value<std::string>()) {
                const auto& level = *log_level;
                if (level == "silent") {
                    app_settings._log_level = settings::log_level::silent;
                } else if (level == "warning") {
                    app_settings._log_level = settings::log_level::warning;
                } else if (level == "info") {
                    app_settings._log_level = settings::log_level::info;
                } else if (level == "verbose") {
                    app_settings._log_level = settings::log_level::verbose;
                } else {
                    // not a known value. Switch to verbose!
                    app_settings._log_level = settings::log_level::verbose;
                    std::cout << "warning: Unknown log level (using verbose)\n";
                }
            }

            auto read_string_list = [&_settings = settings](const char* name){
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
                    std::cout << "warning: Both `violation_report` and `violation_ignore` lists found\n";
                    std::cout << "warning: `violation_report` will be ignored in favor of `violation_ignore`\n";
                }
            }
                

            if (log_level_at_least(settings::log_level::info)) {
                std::cout << "info: ORC config file: " << config_path.string() << "\n";
            }
        } catch (const toml::parse_error& err) {
            std::cerr << "Parsing failed:\n" << err << "\n";
            throw std::runtime_error("configuration parsing error");
        }
    } else {
        std::cout << "ORC config file: not found\n";
    }
}

/**************************************************************************************************/

auto derive_filelist_file_list(const std::filesystem::path& filelist) {
    std::vector<std::filesystem::path> result;
    std::ifstream input(filelist, std::ios::binary);
    
    if (!input) throw std::runtime_error("problem opening filelist for reading");

    static constexpr auto buffer_sz{1024};
    std::array<char, buffer_sz> buffer;

    // The link file list contains object files, one per line.
    while (input) {
        input.getline(&buffer[0], buffer_sz);
        if (strnlen(&buffer[0], 1024))
            result.push_back(&buffer[0]);
    }

    return result;
}

/**************************************************************************************************/

auto find_artifact(std::string_view type,
                   const std::vector<std::filesystem::path>& directories,
                   std::string_view artifact) {
    for (const auto& dir : directories) {
        std::filesystem::path candidate(dir / artifact);
        if (!exists(candidate)) continue;
        return canonical(candidate); // canonical requires the path exists
    }

    if (log_level_at_least(settings::log_level::warning)) {
        cout_safe([&](auto& s){
            s << "warning: Could not find " << type << " '" << artifact << "'\n";
        });
    }

    return std::filesystem::path();
}

/**************************************************************************************************/

struct cmdline_results {
    std::vector<std::filesystem::path> _file_object_list;
    bool _ld_mode{false};
    bool _libtool_mode{false};
};

/**************************************************************************************************/

auto process_command_line(int argc, char** argv) {
    // The calls to std::cout in this routine don't need to be threadsafe (too early)

    cmdline_results result;

    if (log_level_at_least(settings::log_level::verbose)) {
        std::cout << "verbose: arguments:\n";
        for (std::size_t i{0}; i < argc; ++i) {
            std::cout << "  " << argv[i] << '\n';
        }
    }

    if (settings::instance()._standalone_mode) {
        for (std::size_t i{1}; i < argc; ++i) {
            result._file_object_list.push_back(argv[i]);
        }
    } else {
        std::vector<std::filesystem::path> library_search_paths;
        std::vector<std::filesystem::path> framework_search_paths;

        std::vector<std::string> unresolved_libraries;
        std::vector<std::string> unresolved_frameworks;

        for (std::size_t i{1}; i < argc; ++i) {
            std::string_view arg = argv[i];
            if (arg == "-o") {
                std::string filename(argv[++i]);

                // next argument is the output file. Use its name to decide
                // whether we should run in ld or libtool mode. If the mode
                // has already been detected, we can skip this step.
                if (!result._libtool_mode && !result._ld_mode) {
                    if (filename.ends_with(".a")) {
                        result._libtool_mode = true;
                        if (log_level_at_least(settings::log_level::verbose)) {
                            std::cout << "verbose: mode: libtool (by filename)\n";
                        }
                    } else {
                        result._ld_mode = true;
                        if (log_level_at_least(settings::log_level::verbose)) {
                            std::cout << "verbose: mode: ld (by filename)\n";
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
                    std::cout << "verbose: mode: libtool (static)\n";
                }
            } else if (arg == "-target") {
                result._ld_mode = true;
                assert(!result._libtool_mode);
                if (log_level_at_least(settings::log_level::verbose)) {
                    std::cout << "verbose: mode: ld (target)\n";
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
            } else if (arg.ends_with(".o") || arg.ends_with(".a")) {
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

/**************************************************************************************************/

auto epilogue(bool exception) {
    // The calls to std::cout in this routine don't need to be threadsafe (too late)

    const auto& g = globals::instance();

    // If we were showing progress this session, take all the stored up ODRVs and output them
    if (settings::instance()._show_progress) {
        assert(false); // this code is currently broken, and needs rework :\
        // cout_safe([&](auto& s){
        //     s << '\n';
        //     s << odrv_sstream().str();
        // });
    }

    if (log_level_at_least(settings::log_level::info)) {
        cout_safe([&](auto& s){
            s << "info: ORC complete " << g._odrv_count << " ODRVs reported\n";
        });

        if (log_level_at_least(settings::log_level::verbose)) {
            cout_safe([&](auto& s){
            s   << "verbose: additional stats:\n"
                << "  " << g._object_file_count << " compilation units processed\n"
                << "  " << g._die_processed_count << " dies processed\n"
                << "  " << g._die_registered_count << " dies registered\n";
            });
        }
    }

    if (exception || g._odrv_count != 0) {
        return settings::instance()._graceful_exit ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    if (g._fp.is_open()) {
        globals::instance()._fp.close();
    }

    return EXIT_SUCCESS;
}

/**************************************************************************************************/

auto interrupt_callback_handler(int signum) {
    (void)epilogue(true); // let's consider an interrupt an exceptional situation.
    std::exit(signum);
}

/**************************************************************************************************/

void maybe_forward_to_linker(int argc, char** argv, const cmdline_results& cmdline) {
    // The calls to std::cout in this routine don't need to be threadsafe (too early)

    if (!settings::instance()._forward_to_linker) return;

    std::filesystem::path executable_path = rstrip(exec("xcode-select -p")) + "/Toolchains/XcodeDefault.xctoolchain/usr/bin/";

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
        if (log_level_at_least(settings::log_level::warning)) {
            std::cout << "warning: libtool/ld mode could not be derived; forwarding to linker disabled\n";
        }

        return;
    }

    if (!exists(executable_path)) {
        throw std::runtime_error("Could not forward to linker: " + executable_path.string());
    } else if (log_level_at_least(settings::log_level::verbose)) {
        std::cout << "verbose: forwarding to " + executable_path.string() + "\n";
    }

    std::string arguments = executable_path.string();
    for (std::size_t i{1}; i < argc; ++i) {
        // Gotta re-add the escape characters into our params that have spaces in them.
        arguments += std::string(" ") + join(split(argv[i], " "), "\\ ");
    }

    // REVISIT: (fbrereto) We may need to capture/forward stderr here as well.
    // Also, if the execution of the linker ended in a failure, we need to bubble
    // that up immediately, and preempt ORC processing.
    std::cout << exec(arguments.c_str());
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

int main(int argc, char** argv) try {
    signal(SIGINT, interrupt_callback_handler);

    process_orc_config_file(argv[0]);

    auto cmdline = process_command_line(argc, argv);
    const auto& file_list = cmdline._file_object_list;

    if (settings::instance()._print_object_file_list) {
        for (const auto& input_path : file_list) {
            // no need to block on this cout; it's too early
            std::cout << input_path.string() << '\n';
        }

        return EXIT_SUCCESS;
    }

    maybe_forward_to_linker(argc, argv, cmdline);

    if (file_list.empty()) {
        throw std::runtime_error("ORC could not find files to process");
    }

    for (const auto& report : orc_process(file_list)) {
        std::cout << report;   // important to NOT add the '\n', because lots of reports are empty, and it creates a lot of blank lines
    }

    std::vector<pool_string>& paths = unsafe_odrv_paths();
    std::sort(paths.begin(), paths.end());
    auto it = std::unique(paths.begin(), paths.end());
    paths.resize(std::distance(paths.begin(), it));
    
    for(pool_string ps : paths) {
        std::cout << "Path: " << ps << "\n";
    }
    std::cout << "Count=" << paths.size() << "\n";
        
    return epilogue(false);
} catch (const std::exception& error) {
    std::cerr << "Fatal error: " << error.what() << '\n';
    return epilogue(true);
} catch (...) {
    std::cerr << "Fatal error: unknown\n";
    return epilogue(false);
}

/**************************************************************************************************/
