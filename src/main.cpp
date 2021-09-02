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
#include <toml.hpp>

// tbb
#include <tbb/concurrent_unordered_map.h>

// application
#include "orc/features.hpp"
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
}

template <class F>
void cout_safe(F&& f) {
    ostream_safe(std::cout, std::forward<F>(f));
}

/**************************************************************************************************/

const char* problem_prefix() { return settings::instance()._graceful_exit ? "warning" : "error"; }

/**************************************************************************************************/

const char* demangle(const char* x) {
    // The returned char* is good until the next call to demangle() on the same thread.
    // See: https://gcc.gnu.org/onlinedocs/libstdc++/libstdc++-html-USERS-4.3/a01696.html
    thread_local std::unique_ptr<char, void (*)(void*)> hold{nullptr, &free};
    int status = 0;
    char* p = abi::__cxa_demangle(x, nullptr, nullptr, &status);
    if (!p || status != 0) return x;
    hold.reset(p);
    return p;
}

/**************************************************************************************************/

std::string_view path_to_symbol(std::string_view path) {
    // lop off the prefix. In most cases it'll be "::[u]::" - in some it'll be just "::[u]" (in
    // which case there is no symbol path - it's a top-level compilation unit.) We assume the path
    // here starts with one of the two options above, and so return the result based on the string
    // length.

    return path.size() < 7 ? std::string_view() : path.substr(7);
}

/**************************************************************************************************/

template <class Container, class T>
bool sorted_has(const Container& c, const T& x) {
    auto found = std::lower_bound(c.begin(), c.end(), x);
    return found != c.end() && *found == x;
}

/**************************************************************************************************/

auto& odrv_sstream() {
    assert(settings::instance()._show_progress);
    static std::stringstream s;
    return s;
}

std::ostream& odrv_ostream() {
    if (settings::instance()._show_progress) {
        return odrv_sstream();
    } else {
        return std::cout;
    }
}

/**************************************************************************************************/

void report_odrv(const std::string_view& symbol, const die& a, const die& b, dw::at name) {
    std::string odrv_category("tag");

    if (name != dw::at::orc_tag) {
        odrv_category = to_string(a._tag) + std::string(":") + to_string(name);
    }

    auto& settings = settings::instance();
    bool do_report{true};

    if (!settings._violation_ignore.empty()) {
        // Report everything except the stuff on the ignore list
        do_report = !sorted_has(settings._violation_ignore, odrv_category);
    } else if (!settings._violation_report.empty()) {
        // Report nothing except the the stuff on the report list
        do_report = sorted_has(settings._violation_report, odrv_category);
    }

    if (!do_report) return;

    // The number of unique ODRVs is tricky.
    // If A, B, and C are types, and C is different, then if we scan:
    // A, B, then C -> 1 ODRV, but C, then A, B -> 2 ODRVs. Complicating
    // this is that (as this comment is written) we don't detect supersets.
    // So if C has more methods than A and B it may not be detected.
    static std::set<std::size_t> unique_odrv_types;
    std::size_t symbol_hash = hash_combine(0, symbol, odrv_category);
    bool did_insert = unique_odrv_types.insert(symbol_hash).second;
    if (!did_insert) return; // We have already reported an instance of this.

    ostream_safe(odrv_ostream(), [&](auto& s){
        s << problem_prefix() << ": ODRV (" << odrv_category << "); conflict in `"
          << demangle(symbol.data()) << "`\n";
        s << "    " << a << '\n';
        s << "    " << b << '\n';
        s << "\n";
    });

    ++globals::instance()._odrv_count;

    if (settings::instance()._max_violation_count > 0 &&
        globals::instance()._odrv_count >= settings::instance()._max_violation_count) {
        throw std::runtime_error("ODRV limit reached");
    }
}

/**************************************************************************************************/

bool nonfatal_attribute(dw::at at) {
    static const auto attributes = []{
        std::vector<dw::at> nonfatal_attributes = {
            dw::at::accessibility,
            dw::at::apple_block,
            dw::at::apple_flags,
            dw::at::apple_isa,
            dw::at::apple_major_runtime_vers,
            dw::at::apple_objc_complete_type,
            dw::at::apple_objc_direct,
            dw::at::apple_omit_frame_ptr,
            dw::at::apple_optimized,
            dw::at::apple_property,
            dw::at::apple_property_attribute,
            dw::at::apple_property_getter,
            dw::at::apple_property_name,
            dw::at::apple_property_setter,
            dw::at::apple_runtime_class,
            dw::at::apple_sdk,
            dw::at::call_column,
            dw::at::call_file,
            dw::at::call_line,
            dw::at::call_origin,
            dw::at::call_return_pc,
            dw::at::containing_type,
            dw::at::decl_column,
            dw::at::decl_file,
            dw::at::decl_line,
            dw::at::frame_base,
            // According to section 2.17 of the DWARF spec, if high_pc is a constant (e.g., form
            // data4) then its value is the size of the function. Likewise, its existence implies
            // the function it describes is a contiguous block of code in the object file. Since we
            // assume this attribute is of constant form, this is the size of the function. If two
            // or more functions with the same name have different high_pc values, their sizes are
            // different, which means their definitions are going to be different, and that's an
            // ODRV.
            // dw::at::high_pc,
            dw::at::location,
            dw::at::low_pc,
            dw::at::name,
            dw::at::prototyped,
        };

        std::sort(nonfatal_attributes.begin(), nonfatal_attributes.end());

        return nonfatal_attributes;
    }();

    return sorted_has(attributes, at);
}

/**************************************************************************************************/

const die& lookup_die(const dies& dies, std::uint32_t offset) {
    auto found = std::lower_bound(dies.begin(), dies.end(), offset,
                                  [](const auto& x, const auto& offset){
        return x._debug_info_offset < offset;
    });
    bool match = found != dies.end() && found->_debug_info_offset == offset;
    assert(match);
    if (!match) throw std::runtime_error("die not found");
    return *found;
}

/**************************************************************************************************/

const die& find_base_type(const dies& dies, const die& d) {
    if (!d.has_attribute(dw::at::type)) return d;
    return find_base_type(dies, lookup_die(dies, d.attribute_reference(dw::at::type)));
}

/**************************************************************************************************/

void resolve_reference_attributes(const dies& dies, die& d) { // REVISIT (fbrereto): d is an out-arg
    for (auto& attr : d._attributes) {
        if (attr._name == dw::at::type) continue;
        if (!attr.has(attribute_value::type::reference)) continue;
        const die& resolved = lookup_die(dies, attr.reference());
        attr._value.die(resolved);
        attr._value.string(empool(resolved._path));
    }
}

/**************************************************************************************************/

void resolve_type_attribute(const dies& dies, die& d) { // REVISIT (fbrereto): d is an out-arg
    constexpr auto type_k = dw::at::type;

    if (!d.has_attribute(type_k)) return; // nothing to resolve
    if (d._type_resolved) return; // already resolved

    const die& base_type_die = find_base_type(dies, d);

    // Now that we have the resolved type die, overwrite this die's type to reflect
    // the resolved type.
    attribute& type_attr = d.attribute(type_k);
    type_attr._value.die(base_type_die);
    if (base_type_die.has_attribute(dw::at::name)) {
        type_attr._value.string(base_type_die.attribute_string(dw::at::name));
    }

    d._type_resolved = true;
}

/**************************************************************************************************/
bool type_equivalent(const attribute& x, const attribute& y);
dw::at find_die_conflict(const die& x, const die& y) {
    if (x._tag != y._tag) return dw::at::orc_tag;

    const auto& yfirst = y._attributes.begin();
    const auto& ylast = y._attributes.end();

    for (const auto& xattr : x._attributes) {
        auto name = xattr._name;
        if (nonfatal_attribute(name)) continue;

        auto yfound = std::find_if(yfirst, ylast, [&](auto& x) { return name == x._name; });
        if (yfound == ylast) continue; // return name;

        const auto& yattr = *yfound;

        if (name == dw::at::type && type_equivalent(xattr, yattr)) continue;
        else if (xattr == yattr) continue;

        return name;
    }

    // REVISIT (fbrereto) : There may be some attributes left over in y; should we care?
    // If they're not nonfatal attributes, we should...

    return dw::at::none; // they're "the same"
}

/**************************************************************************************************/

bool type_equivalent(const attribute& x, const attribute& y) {
    // types are pretty convoluted, so we pull their comparison out here in an effort to
    // keep it all in a developer's head.

    if (x.has(attribute_value::type::reference) &&
        y.has(attribute_value::type::reference) &&
        x.reference() == y.reference()) {
        return true;
    }

    if (x.has(attribute_value::type::string) &&
        y.has(attribute_value::type::string) &&
        x.string_hash() == y.string_hash()) {
        return true;
    }

    if (x.has(attribute_value::type::die) && y.has(attribute_value::type::die) &&
        find_die_conflict(x.die(), y.die()) == dw::at::none) {
        return true; // Should this change based on find_die_conflict's result?
    }

    // Type mismatch.
    return false;
}

/**************************************************************************************************/

bool skip_tagged_die(const die& d) {
    static const dw::tag skip_tags[] = {
        dw::tag::compile_unit,
        dw::tag::partial_unit,
        dw::tag::variable,
        dw::tag::formal_parameter,
    };
    static const auto first = std::begin(skip_tags);
    static const auto last = std::end(skip_tags);

    return std::find(first, last, d._tag) != last;
}

/**************************************************************************************************/

void enforce_odr(const std::string_view& symbol, const die& x, const die& y) {
#if 0
    if (x._path == "::[arm64]::size_type") {
        int x;
        (void)x;
    }
#endif

    auto conflict_name = find_die_conflict(x, y);

    if (conflict_name != dw::at::none) {
        report_odrv(symbol, x, y, conflict_name);
    }
}

/**************************************************************************************************/

bool skip_die(const dies& dies, die& d, const std::string_view& symbol) {
    // These are a handful of "filters" we use to elide false positives.

    // These are the tags we don't deal with (yet, if ever.)
    if (skip_tagged_die(d)) return true;

    // Empty path means the die (or an ancestor) is anonymous/unnamed. No need to register
    // them.
    if (d._path.empty()) return true;

    // Symbols with __ in them are reserved, so are not user-defined. No need to register
    // them.
    if (d._path.find("::__") != std::string::npos) return true;

    // lambdas are ephemeral and can't cause (hopefully) an ODRV
    if (d._path.find("lambda") != std::string::npos) return true;

    // we don't handle any die that's ObjC-based.
    if (d.has_attribute(dw::at::apple_runtime_class)) return true;

    // If the symbol is listed in the symbol_ignore list, we're done here.
    if (sorted_has(settings::instance()._symbol_ignore, symbol)) return true;

    // Unfortunately we have to do this work to see if we're dealing with
    // a self-referential type.
    if (d.has_attribute(dw::at::type)) {
        resolve_type_attribute(dies, d);
        const auto& type = d.attribute(dw::at::type);
        // if this is a self-referential type, it's die will have no attributes.
        if (type.die()._attributes.empty()) return true;
    }

    return false;
}

/**************************************************************************************************/

void update_progress() {
    if (!settings::instance()._show_progress) return;

    std::size_t done = globals::instance()._die_analyzed_count;
    std::size_t total = globals::instance()._die_processed_count;
    std::size_t percentage = static_cast<double>(done) / total * 100;

    cout_safe([&](auto& s){
        s << '\r' << done << "/" << total << "  " << percentage << "%; ";
        s << globals::instance()._odrv_count << " violation(s) found";
        s << "          "; // 10 spaces of overprint to clear out any previous lingerers
    });
}

/**************************************************************************************************/

void register_dies(dies die_vector) {
    // This is a list so the die vectors don't move about. The dies become pretty entangled as they
    // point to one another by reference, and the odr_map itself stores const pointers to the dies
    // it registers. Thus, we move our incoming die_vector to the end of this list, and all the
    // pointers we use will stay valid for the lifetime of the application.
#if ORC_FEATURE(LEAKY_MEMORY)
    auto& dies = *(new decltype(die_vector)(std::move(die_vector)));
#else
    static std::list<dies> dies_collection;
    dies_collection.push_back(std::move(die_vector));
    auto& dies = dies_collection.back();
#endif

    for (auto& d : dies) {
        // save for debugging. Useful to watch for a specific symbol.
#if 0
        if (d._path == "::[u]::_ZNK14example_vtable6object3apiEv") {
            int x;
            (void)x;
        }
#endif

        auto symbol = path_to_symbol(d._path);
        auto should_skip = skip_die(dies, d, symbol);

        if (settings::instance()._print_symbol_paths) {
            static pool_string last_object_file_s;

            cout_safe([&](auto& s){
                if (d._object_file != last_object_file_s) {
                    last_object_file_s = d._object_file;
                    s << '\n' << last_object_file_s << '\n';
                }

                s << (should_skip ? 'S' : 'R') << " - 0x";
                s.width(8);
                s.fill('0');
                s << std::hex << d._debug_info_offset << std::dec << " " << d._path << '\n';
            });
        }

        if (should_skip) continue;

        //
        // After this point, we KNOW we're going to register the die. Do additional work
        // necessary just for DIEs we're registering after this point.
        //

        resolve_reference_attributes(dies, d);

        using map_type = tbb::concurrent_unordered_map<std::size_t, const die*>;

#if ORC_FEATURE(LEAKY_MEMORY)
        static map_type& die_map = *(new map_type());
#else
        static map_type die_map;
#endif
        auto result = die_map.insert(std::make_pair(d._hash, &d));
        if (result.second) {
            ++globals::instance()._die_registered_count;
            continue;
        }

        // possible violation - make sure everything lines up!

        enforce_odr(symbol, d, *result.first->second);
    }

    globals::instance()._die_analyzed_count += dies.size();

    update_progress();
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
            app_settings._print_object_file_list =
                settings["print_object_file_list"].value_or(false);

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

    // The call to std::cout here doesn't need to be threadsafe (too early)
    if (log_level_at_least(settings::log_level::warning)) {
        std::cout << "warning: Could not find " << type << " '" << artifact << "'\n";
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
                // next argument is the output file. Use its name to decide
                // whether we should run in ld or libtool mode.
                if (std::string(argv[++i]).ends_with(".a")) {
                    result._libtool_mode = true;
                    assert(!result._ld_mode);
                    if (log_level_at_least(settings::log_level::verbose)) {
                        std::cout << "verbose: mode: libtool\n";
                    }
                } else {
                    result._ld_mode = true;
                    assert(!result._libtool_mode);
                    if (log_level_at_least(settings::log_level::verbose)) {
                        std::cout << "verbose: mode: ld\n";
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
                    std::cout << "verbose: mode: libtool\n";
                }
            } else if (arg == "-target") {
                result._ld_mode = true;
                if (log_level_at_least(settings::log_level::verbose)) {
                    std::cout << "verbose: mode: ld\n";
                }
                assert(!result._libtool_mode);
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
        std::cout << '\n';
        std::cout << odrv_sstream().str();
    }

    if (log_level_at_least(settings::log_level::info)) {
        std::cout << "info: ORC complete; " << g._odrv_count << " ODRVs reported\n";

        if (log_level_at_least(settings::log_level::verbose)) {
            std::cout << "verbose: additional stats:\n"
                      << "  " << g._object_file_count << " compilation units processed\n"
                      << "  " << g._die_processed_count << " dies processed\n"
                      << "  " << g._die_registered_count << " dies registered\n";
        }
    }

    if (exception || g._odrv_count != 0) {
        return settings::instance()._graceful_exit ? EXIT_SUCCESS : EXIT_FAILURE;
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
    }

    std::string arguments = executable_path.string();
    for (std::size_t i{1}; i < argc; ++i) {
        arguments += std::string(" ") + argv[i];
    }

    // REVISIT: (fbrereto) We may need to capture/forward stderr here as well.
    // Also, if the execution of the linker ended in a failure, we need to bubble
    // that up immediately, and preempt ORC processing.
    std::cout << exec(arguments.c_str());
}

/**************************************************************************************************/

struct work_counter {
    struct state {
        void increment() {
            {
            std::lock_guard<std::mutex> lock(_m);
            ++_n;
            }
            _c.notify_all();
        }

        void decrement() {
            {
            std::lock_guard<std::mutex> lock(_m);
            --_n;
            }
            _c.notify_all();
        }

        void wait() {
            std::unique_lock<std::mutex> lock(_m);
            if (_n == 0) return;
            _c.wait(lock, [&]{ return _n == 0; });
        }

        std::mutex _m;
        std::condition_variable _c;
        std::size_t _n{0};
    };

    using shared_state = std::shared_ptr<state>;

    friend struct token;

public:
    work_counter() : _impl{std::make_shared<state>()} {}

    struct token {
        token(shared_state w) : _w(std::move(w)) { _w->increment(); }
        token(const token& t) : _w{t._w}  { _w->increment(); }
        token(token&& t) = default;
        ~token() { if (_w) _w->decrement(); }
    private:
        shared_state _w;
    };

    auto working() { return token(_impl); }

    void wait() { _impl->wait(); }

private:
    shared_state _impl;

    void increment() { _impl->increment(); }
    void decrement() { _impl->decrement(); }
};

/**************************************************************************************************/

auto& work() {
    static work_counter _work;
    return _work;
}

/**************************************************************************************************/

void do_work(std::function<void()> f){
    if (!settings::instance()._parallel_processing) {
        f();
        return;
    }

    static orc::task_system system;

    system([_work_token = work().working(), _f = std::move(f)] {
        _f();
    });
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

    for (const auto& input_path : file_list) {
        do_work([_input_path = input_path] {
            if (!exists(_input_path)) {
                throw std::runtime_error("file " + _input_path.string() + " does not exist");
            }

            freader input(_input_path);
            callbacks callbacks = {
                register_dies,
                do_work,
                empool,
            };
            parse_file(_input_path.string(), input, input.size(), std::move(callbacks));
        });
    }

    work().wait();

    return epilogue(false);
} catch (const std::exception& error) {
    std::cerr << problem_prefix() << ": " << error.what() << '\n';
    return epilogue(true);
} catch (...) {
    std::cerr << problem_prefix() << ": unknown\n";
    return epilogue(true);
}

/**************************************************************************************************/
