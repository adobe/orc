// stdc++
#include <array>
#include <exception>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <unordered_map>

// posix
#include <fcntl.h>
#include <unistd.h>

// toml++
#include <toml++/toml.h>

// orc
#include <orc/orc.hpp>

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

struct orc_test_settings {
    bool _json_mode{false};
};

auto& settings() {
    static orc_test_settings result;
    return result;
}

/**************************************************************************************************/

std::ostream& console() {
    if (settings()._json_mode) {
        static std::stringstream s;
        return s;
    }

    return std::cout;
}

std::ostream& console_error() {
    if (settings()._json_mode) {
        static std::stringstream s;
        return s;
    }

    return std::cerr;
}

/**************************************************************************************************/

auto& toml_out() {
    static toml::table result;
    return result;
}

/**************************************************************************************************/

} // namespace {

/**************************************************************************************************/

namespace log {

/**************************************************************************************************/

void log(const std::string& type,
         const std::string& message,
         std::optional<std::string> title = std::nullopt,
         std::optional<std::string> filename = std::nullopt) {
    if (settings()._json_mode) {
        toml::table result;
        result.insert("type", type);
        result.insert("message", message);
        if (title) result.insert("title", *title);
        if (filename) result.insert("filename", *filename);
        if (auto* array = toml_out()["log"].as_array()) {
            array->push_back(result);
        } else {
            toml::array new_log;
            new_log.push_back(std::move(result));
            toml_out().insert("log", std::move(new_log));
        }
    } else {
        if (title) {
            console() << *title << ": ";
        }
        console() << message;
        if (filename) {
            console() << " (" << *filename << ")";
        }
        console() << '\n';
    }
}

/**************************************************************************************************/

void notice(const std::string& message,
            std::optional<std::string> title = std::nullopt,
            std::optional<std::string> filename = std::nullopt) {
    log("notice", message, title, filename);
}

/**************************************************************************************************/

void warning(const std::string& message,
             std::optional<std::string> title = std::nullopt,
             std::optional<std::string> filename = std::nullopt) {
    log("warning", message, title, filename);
}

/**************************************************************************************************/

void error(const std::string& message,
           std::optional<std::string> title = std::nullopt,
           std::optional<std::string> filename = std::nullopt) {
    log("error", message, title, filename);
}

/**************************************************************************************************/

} // namespace log

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

void assume(bool condition, std::string message) {
    if (condition) return;
    throw std::runtime_error(message);
}

/**************************************************************************************************/

struct compilation_unit {
    std::filesystem::path _src;
    std::string _object_file_name;
    std::vector<std::string> _flags;
    // temporary object file details
    std::optional<int> _fd;
    std::optional<std::string> _path; // only set this if you want to delete the object file

    ~compilation_unit() {
        if (_fd) {
            close(*_fd);
        }
        if (_path) {
            unlink(_path->c_str());
        }
    }
};

/**************************************************************************************************/

auto object_file_path(const std::filesystem::path& battery_path, const compilation_unit& unit) {
    auto stem =
        !unit._object_file_name.empty() ? unit._object_file_name : unit._src.stem().string();
    auto result = std::filesystem::temp_directory_path() / "orc_test" / battery_path.filename() /
                  (stem + ".obj");
    create_directories(result.parent_path());
    return result;
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

std::string rstrip(std::string s) {
    auto found =
        std::find_if_not(s.rbegin(), s.rend(), [](char x) { return std::isspace(x) || x == 0; });
    s.erase(s.size() - std::distance(found.base(), s.end()));
    return s;
}

/**************************************************************************************************/

auto path_to_clang() {
    return rstrip(exec("xcode-select -p")) + "/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++";
}

/**************************************************************************************************/

struct expected_odrv {
    std::unordered_map<std::string, std::string> _map;

    const std::string& operator[](const std::string& key) const {
        auto found = _map.find(key);
        if (found == _map.end()) {
            static const std::string no_entry;
            return no_entry;
        }
        return found->second;
    }

    const std::string& category() const { return (*this)["category"]; }
    const std::string& symbol() const { return (*this)["symbol"]; }
    const std::string& linkage_name() const { return (*this)["linkage_name"]; }
};

std::ostream& operator<<(std::ostream& s, const expected_odrv& x) {
    // map is unordered, so we have to sort the keys...
    std::vector<std::string> keys;
    for (const auto& entry : x._map) {
        keys.push_back(entry.first);
    }
    std::sort(keys.begin(), keys.end());
    for (const auto& key : keys) {
        s << "    " << key << ": " << x._map.find(key)->second << '\n';
    }
    return s;
}

/**************************************************************************************************/

const char* to_string(toml::node_type x) {
    switch (x) {
        case toml::node_type::none:
            return "none";
        case toml::node_type::table:
            return "table";
        case toml::node_type::array:
            return "array";
        case toml::node_type::string:
            return "string";
        case toml::node_type::integer:
            return "integer";
        case toml::node_type::floating_point:
            return "floating_point";
        case toml::node_type::boolean:
            return "boolean";
        case toml::node_type::date:
            return "date";
        case toml::node_type::time:
            return "time";
        case toml::node_type::date_time:
            return "date_time";
    }
    assert(false);
}

/**************************************************************************************************/

void validate_compilation_unit(const compilation_unit& unit) {
    if (!exists(unit._src)) {
        throw std::runtime_error("source file " + unit._src.string() + " does not exist");
    }
}

/**************************************************************************************************/

std::vector<compilation_unit> derive_compilation_units(const std::filesystem::path& home,
                                                       const toml::table& settings) {
    std::vector<compilation_unit> result;
    const toml::array* arr = settings["source"].as_array();
    if (!arr) return result;
    for (const toml::node& src_node : *arr) {
        const toml::table* src_ptr = src_node.as_table();
        if (!src_ptr) {
            throw std::runtime_error(std::string("expected a source table, found: ") +
                                     to_string(src_node.type()));
        }
        const toml::table& src = *src_ptr;
        compilation_unit unit;

        std::optional<std::string_view> path = src["path"].value<std::string_view>();
        if (!path) {
            throw std::runtime_error("Missing required source key \"path\"");
        }
        unit._src = home / *path;

        if (std::optional<std::string_view> obj =
                src["object_file_name"].value<std::string_view>()) {
            unit._object_file_name = *obj;
        }

        if (const toml::array* flags = src["flags"].as_array()) {
            for (const auto& flag : *flags) {
                if (std::optional<std::string_view> flag_str = flag.value<std::string_view>()) {
                    unit._flags.emplace_back(*flag_str);
                }
            }
        }

        validate_compilation_unit(unit);
        result.push_back(unit);
    }
    return result;
}

/**************************************************************************************************/
// REVISIT (fosterbrereton): units is an out-arg here (_path can get modified)
std::vector<std::filesystem::path> compile_compilation_units(const std::filesystem::path& home,
                                                             const toml::table& settings,
                                                             std::vector<compilation_unit>& units) {
    std::vector<std::filesystem::path> object_files;
    const bool preserve_object_files =
        settings["orc_test_flags"]["preserve_object_files"].value_or(false);
    console() << "Compiling " << units.size() << " source file(s):\n";
    for (auto& unit : units) {
        auto temp_path = object_file_path(home, unit);
        if (preserve_object_files) {
            console() << temp_path << '\n';
        } else {
            unit._path = temp_path;
        }
        std::string command(path_to_clang());
        for (const auto& flag : unit._flags) {
            command += " " + flag;
        }
        command += " -g -c " + unit._src.string() + " -o " + temp_path.string();
        // Save this for debugging purposes.
        // console() << command << '\n';
        std::string result = exec(command.c_str());
        if (!result.empty()) {
            console() << result;
            throw std::runtime_error("unexpected compilation failure");
        }
        object_files.emplace_back(std::move(temp_path));
        console() << "    " << unit._src.filename() << " -> " << object_files.back().filename() << '\n';
    }
    return object_files;
}

/**************************************************************************************************/

std::vector<expected_odrv> derive_expected_odrvs(const std::filesystem::path& home,
                                                 const toml::table& settings) {
    std::vector<expected_odrv> result;
    const toml::array* arr = settings["odrv"].as_array();
    if (!arr) return result;
    for (const toml::node& odrv_node : *arr) {
        const toml::table* odrv_ptr = odrv_node.as_table();
        if (!odrv_ptr) {
            throw std::runtime_error(std::string("expected a source table, found: ") +
                                     to_string(odrv_node.type()));
        }
        const toml::table& src = *odrv_ptr;
        expected_odrv odrv;

        for (const auto& pair : src) {
            if (!pair.second.is_string()) continue;
            odrv._map[pair.first] = *pair.second.value<std::string>();
        }

        if (odrv._map.count("category") == 0) {
            throw std::runtime_error("Missing required odrv key \"category\"");
        }

        result.push_back(odrv);
    }
    return result;
}

/**************************************************************************************************/

bool odrv_report_match(const expected_odrv& odrv, const odrv_report& report) {
    if (odrv.category() != report.category()) {
        return false;
    }

    const auto& symbol = odrv.symbol();
    if (!symbol.empty() && symbol != demangle(report._symbol.data())) {
        return false;
    }

    const std::string& linkage_name = odrv.linkage_name();
    if (!linkage_name.empty()) {
        const pool_string report_linkage_name = report._list_head->attribute_string(dw::at::linkage_name);
        if (linkage_name != report_linkage_name.view())
            return false;
    }
    return true;
}

/**************************************************************************************************/

constexpr const char* tomlname_k = "odrv_test.toml";

/**************************************************************************************************/

void run_battery_test(const std::filesystem::path& home) {
    static bool first_s = false;

    if (!first_s) {
        console() << '\n';
    } else {
        first_s = false;
    }

    assume(is_directory(home), "\"" + home.string() + "\" is not a directory");
    std::filesystem::path tomlpath = home / tomlname_k;
    assume(is_regular_file(tomlpath), "\"" + tomlpath.string() + "\" is not a regular file");
    toml::table settings;

    console() << "-=-=- Test: " << home << "\n";

    try {
        settings = toml::parse_file(tomlpath.string());
    } catch (const toml::parse_error& error) {
        console_error() << error << '\n';
        throw std::runtime_error("settings file parsing error");
    }

    // Save this for debugging purposes.
    // console_error() << toml::json_formatter{settings} << '\n';

    const bool skip_test = settings["orc_test_flags"]["disable"].value_or(false);

    if (skip_test) {
        log::notice("test disabled");
        return;
    }

    auto compilation_units = derive_compilation_units(home, settings);
    if (compilation_units.empty()) {
        throw std::runtime_error("found no sources to compile");
    }

    auto expected_odrvs = derive_expected_odrvs(home, settings);
    if (expected_odrvs.empty()) {
        log::notice("Found no expected ODRVs for this test");
    }

    auto object_files = compile_compilation_units(home, settings, compilation_units);

    orc_reset();
    auto reports = orc_process(object_files);

    console() << "ODRVs expected: " << expected_odrvs.size() << "; reported: " << reports.size() << '\n';

    toml::table result;
    result.insert("expected", static_cast<toml::int64_t>(expected_odrvs.size()));
    result.insert("reported", static_cast<toml::int64_t>(reports.size()));
    toml_out().insert(home.stem().string(), std::move(result));

    // At this point, the reports.size() should match the expected_odrvs.size()
    bool unexpected_result = false;
    if (expected_odrvs.size() != reports.size()) {
        unexpected_result = true;
    } else {
        for (const auto& report : reports) {
            auto found =
                std::find_if(expected_odrvs.begin(), expected_odrvs.end(),
                             [&](const auto& odrv) {
                        return odrv_report_match(odrv, report);
                });

            if (found == expected_odrvs.end()) {
                unexpected_result = true;
                break;
            }

            console() << "    Found expected ODRV: " << report.category() << "\n";
        }
    }

    if (unexpected_result) {
        console_error() << "Reported ODRV(s):\n";

        // If there's an error in the test, dump what we've found to assist debugging.
        for (const auto& report : reports) {
            console() << report << '\n';
        }

        console_error() << "Expected ODRV(s):\n";
        std::size_t count{0};
        for (const auto& expected : expected_odrvs) {
            console() << ++count << ":\n" << expected << '\n';
        }

        throw std::runtime_error("ODRV count mismatch");
    }
}

/**************************************************************************************************/

void traverse_directory_tree(std::filesystem::path& directory) {
    assert(is_directory(directory));

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        try {
            if (is_directory(entry)) {
                std::filesystem::path path = entry.path();

                if (exists(path / tomlname_k)) {
                    run_battery_test(path);
                }

                traverse_directory_tree(path);
            }
        } catch (...) {
            console_error() << "\nIn battery " << entry.path() << ":";
            throw;
        }
    }
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

int main(int argc, char** argv) try {
    if (argc < 2) {
        console_error() << "Usage: " << argv[0] << " /path/to/test/battery/\n";
        throw std::runtime_error("no path to test battery given");
    }

    std::filesystem::path battery_path{argv[1]};

    if (!exists(battery_path) || !is_directory(battery_path)) {
        throw std::runtime_error("test battery path is missing or not a directory");
    }

    settings()._json_mode = argc > 2 && std::string(argv[2]) == "--json_mode";

    traverse_directory_tree(battery_path);

    if (settings()._json_mode) {
        std::cout << toml::json_formatter{ toml_out() } << '\n';
    }

    return EXIT_SUCCESS;
} catch (const std::exception& error) {
    log::error(error.what(), "Fatal error");
    return EXIT_FAILURE;
} catch (...) {
    log::error("unknown", "Fatal error");
    return EXIT_FAILURE;
}

/**************************************************************************************************/
