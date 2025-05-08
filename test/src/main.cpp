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
#include <orc/dwarf.hpp>
#include <orc/macho.hpp>
#include <orc/orc.hpp>
#include <orc/tracy.hpp>

// Google Test
#include <gtest/gtest.h>

//--------------------------------------------------------------------------------------------------

namespace {

//--------------------------------------------------------------------------------------------------

struct orc_test_settings {
    bool _json_mode{false};
};

auto& test_settings() {
    static orc_test_settings result;
    return result;
}

//--------------------------------------------------------------------------------------------------

std::ostream& console() {
    if (test_settings()._json_mode) {
        static std::stringstream s;
        return s;
    }

    return std::cout;
}

std::ostream& console_error() {
    if (test_settings()._json_mode) {
        static std::stringstream s;
        return s;
    }

    return std::cerr;
}

//--------------------------------------------------------------------------------------------------

auto& toml_out() {
    static toml::table result;
    return result;
}

//--------------------------------------------------------------------------------------------------

} // namespace

//--------------------------------------------------------------------------------------------------

namespace logging {

//--------------------------------------------------------------------------------------------------

void log(const std::string& type,
         const std::string& message,
         std::optional<std::string> title = std::nullopt,
         std::optional<std::string> filename = std::nullopt) {
    if (test_settings()._json_mode) {
        toml::table result;
        result.insert("type", type);
        result.insert("message", message);
        if (title) result.insert("title", *title);
        if (filename) result.insert("filename", *filename);
        if (auto* array = toml_out()["_orc_test_log"].as_array()) {
            array->push_back(result);
        } else {
            toml::array new_log;
            new_log.push_back(std::move(result));
            toml_out().insert("_orc_test_log", std::move(new_log));
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

//--------------------------------------------------------------------------------------------------

void notice(const std::string& message,
            std::optional<std::string> title = std::nullopt,
            std::optional<std::string> filename = std::nullopt) {
    log("notice", message, title, filename);
}

//--------------------------------------------------------------------------------------------------

void warning(const std::string& message,
             std::optional<std::string> title = std::nullopt,
             std::optional<std::string> filename = std::nullopt) {
    log("warning", message, title, filename);
}

//--------------------------------------------------------------------------------------------------

void error(const std::string& message,
           std::optional<std::string> title = std::nullopt,
           std::optional<std::string> filename = std::nullopt) {
    log("error", message, title, filename);
}

//--------------------------------------------------------------------------------------------------

} // namespace logging

//--------------------------------------------------------------------------------------------------

namespace {

//--------------------------------------------------------------------------------------------------

void assume(bool condition, std::string message) {
    if (condition) return;
    throw std::runtime_error(message);
}

//--------------------------------------------------------------------------------------------------

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

//--------------------------------------------------------------------------------------------------

auto object_file_path(const std::filesystem::path& battery_path, const compilation_unit& unit) {
    auto stem =
        !unit._object_file_name.empty() ? unit._object_file_name : unit._src.stem().string();
    auto result = std::filesystem::temp_directory_path() / "orc_test" / battery_path.filename() /
                  (stem + ".obj");
    create_directories(result.parent_path());
    return result;
}

//--------------------------------------------------------------------------------------------------

std::string exec(std::string cmd) {
    cmd += " 2>&1";
    std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd.c_str(), "r"), pclose);

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

//--------------------------------------------------------------------------------------------------

std::string rstrip(std::string s) {
    auto found =
        std::find_if_not(s.rbegin(), s.rend(), [](char x) { return std::isspace(x) || x == 0; });
    s.erase(s.size() - std::distance(found.base(), s.end()));
    return s;
}

//--------------------------------------------------------------------------------------------------

auto path_to_clang() {
    return rstrip(exec("xcode-select -p")) + "/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang++";
}

//--------------------------------------------------------------------------------------------------

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

#if 0
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
#endif

//--------------------------------------------------------------------------------------------------

const char* to_string(toml::node_type x) {
    // clang-format off
    switch (x) {
        case toml::node_type::none: return "none";
        case toml::node_type::table: return "table";
        case toml::node_type::array: return "array";
        case toml::node_type::string: return "string";
        case toml::node_type::integer: return "integer";
        case toml::node_type::floating_point: return "floating_point";
        case toml::node_type::boolean: return "boolean";
        case toml::node_type::date: return "date";
        case toml::node_type::time: return "time";
        case toml::node_type::date_time: return "date_time";
    }
    // clang-format on
    assert(false);
}

//--------------------------------------------------------------------------------------------------

void validate_compilation_unit(const compilation_unit& unit) {
    if (!exists(unit._src)) {
        throw std::runtime_error("source file " + unit._src.string() + " does not exist");
    }
}

//--------------------------------------------------------------------------------------------------
/// rummage through the settings toml for any object file specifications (that is, object files
/// included as part of the test, not ones that first need to be compiled from source.)
std::vector<std::filesystem::path> derive_object_files(const std::filesystem::path& home,
                                                       const toml::table& settings) {
    std::vector<std::filesystem::path> result;
    const toml::array* arr = settings["object"].as_array();
    if (!arr) return result;
    for (const toml::node& src_node : *arr) {
        const toml::table* src_ptr = src_node.as_table();
        if (!src_ptr) {
            throw std::runtime_error(std::string("expected an object table, found: ") +
                                     to_string(src_node.type()));
        }
        const toml::table& src = *src_ptr;
        std::optional<std::string_view> path = src["path"].value<std::string_view>();
        if (path) {
            result.push_back(home / *path);
        }
    }
    return result;
}

//--------------------------------------------------------------------------------------------------

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

//--------------------------------------------------------------------------------------------------
/// Returns a command-line sanitized version of the string, to prevent user input from doing
/// something troublesome with the command line execution of the compiler. Note this could
/// also wreak havoc on non-7-bit-ASCII locales. If that becomes a big problem this can be
/// improved.
std::string sanitize(const std::filesystem::path& in) {
    std::string result = in.string();
    auto new_end = std::remove_if(result.begin(), result.end(), [](char c) {
        return !(std::isalnum(c) || c == '/' || c == '.' || c == '_');
    });
    result.erase(new_end, result.end());
    return result;
}

//--------------------------------------------------------------------------------------------------
// REVISIT (fosterbrereton): units is an out-arg here (_path can get modified)
std::vector<std::filesystem::path> compile_compilation_units(const std::filesystem::path& home,
                                                             const toml::table& settings,
                                                             std::vector<compilation_unit>& units) {
    std::vector<std::filesystem::path> object_files;
    const bool preserve_object_files =
        settings["orc_test_flags"]["preserve_object_files"].value_or(false);
    // console() << "Compiling " << units.size() << " source file(s):\n";
    for (auto& unit : units) {
        auto temp_path = sanitize(object_file_path(home, unit));
        if (preserve_object_files) {
            console() << temp_path << '\n';
        } else {
            unit._path = temp_path;
        }
        std::string command(path_to_clang());
        for (const auto& flag : unit._flags) {
            command += " " + flag;
        }
        command += " -g -c " + sanitize(unit._src) + " -o " + temp_path;
        // Save this for debugging purposes.
        // console() << command << '\n';
        std::string result = exec(command.c_str());
        if (!result.empty()) {
            console() << result;
            throw std::runtime_error("unexpected compilation failure");
        }
        object_files.emplace_back(std::move(temp_path));
        // console() << "    " << unit._src.filename() << " -> " << object_files.back().filename()
        //          << '\n';
    }
    return object_files;
}

//--------------------------------------------------------------------------------------------------

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

//--------------------------------------------------------------------------------------------------

bool odrv_report_match(const expected_odrv& odrv, const odrv_report& report) {
    if (odrv.category() != report.reporting_categories()) {
        return false;
    }

    const auto& symbol = odrv.symbol();
    if (!symbol.empty() && symbol != demangle(report._symbol.data())) {
        return false;
    }

    const std::string& linkage_name = demangle(odrv.linkage_name().c_str());
    if (!linkage_name.empty()) {
        const auto& die_pair = report.conflict_map().begin()->second;
        const char* report_linkage_name =
            demangle(die_pair._attributes.string(dw::at::linkage_name).view().begin());
        if (linkage_name != report_linkage_name) return false;
    }
    return true;
}

//--------------------------------------------------------------------------------------------------
/**
 * @brief Validates runtime metrics against expected values defined in settings
 *
 * This function compares various metrics collected during an ORC test pass
 * against expected values specified in the TOML configuration. It reports
 * any mismatches to the error console.
 *
 * @param settings The TOML configuration table containing expected metric values
 *
 * @pre The settings parameter should contain a "metrics" table with integer values
 *      for the metrics to be validated
 * @pre The globals singleton should be initialized with the actual metrics
 *
 * @return true if any validation failures occurred (metrics didn't match expected values)
 * @return false if all metrics matched or if no metrics table was found in settings
 */
bool metrics_validation(const toml::table& settings) {
    const toml::table* expected_ptr = settings["metrics"].as_table();

    if (!expected_ptr) {
        return false;
    }

    const toml::table& expected = *expected_ptr;
    const globals& metrics = globals::instance();
    bool failure = false;

    const auto compare_field = [&expected](const std::atomic_size_t& field,
                                           const char* key) -> bool {
        const toml::value<int64_t>* file_count_ptr = expected[key].as_integer();
        if (!file_count_ptr) return false;
        int64_t expected = **file_count_ptr;
        if (expected == field) return false;
        console_error() << key << " mismatch (expected " << expected << "; calculated " << field
                        << ")\n";
        return true;
    };

    failure += compare_field(metrics._object_file_count, "object_file_count");
    failure += compare_field(metrics._odrv_count, "odrv_count");
    failure += compare_field(metrics._unique_symbol_count, "unique_symbol_count");
    failure += compare_field(metrics._die_processed_count, "die_processed_count");
    failure += compare_field(metrics._die_skipped_count, "die_skipped_count");

    return failure;
}

//--------------------------------------------------------------------------------------------------

constexpr const char* tomlname_k = "odrv_test.toml";

//--------------------------------------------------------------------------------------------------
/**
 * @brief Test fixture for ORC tests
 *
 * This class represents a test fixture for running ORC tests on a specific test directory.
 * It handles:
 * - Loading and parsing the test configuration from a TOML file
 * - Compiling source files if needed
 * - Processing object files to detect ODR violations
 * - Validating metrics and ODRV reports against expected values
 */
class orc_test_instance : public ::testing::Test {
    std::filesystem::path _path;

public:
    explicit orc_test_instance(std::filesystem::path&& path) : _path(std::move(path)) {}

protected:
    void SetUp() override { orc_reset(); }

    void TestBody() override;
};

//--------------------------------------------------------------------------------------------------
/**
 * @brief Implements the logic for an ORC test
 *
 * This method executes the main logic for an ORC test:
 * 1. Validates and loads the test configuration from a TOML file
 * 2. Compiles source files (if any are specified)
 * 3. Collects object files for processing (if any are specified)
 * 4. Processes all object files to detect ODR violations
 * 5. Validates metrics against expected values
 * 6. Validates detected ODR violations against expected violations
 *
 * The test will be skipped if the "disable" flag is set in the configuration.
 *
 * @pre The `_path` member must point to a valid directory containing a valid TOML configuration
 * file
 * @pre The TOML file must follow the expected format for ORC test configuration
 * @post Test assertions are made to validate metrics and ODR violation reports
 * @post If the test is disabled in configuration, it will be skipped
 * @throws std::runtime_error If the TOML file cannot be parsed or other critical errors occur
 */
void orc_test_instance::TestBody() {
    assume(std::filesystem::is_directory(_path), "\"" + _path.string() + "\" is not a directory");
    std::filesystem::path tomlpath = _path / tomlname_k;
    assume(std::filesystem::is_regular_file(tomlpath),
           "\"" + tomlpath.string() + "\" is not a regular file");
    toml::table settings;

    try {
        settings = toml::parse_file(tomlpath.string());
    } catch (const toml::parse_error& error) {
        console_error() << error << '\n';
        throw std::runtime_error("settings file parsing error");
    }

    if (settings["orc_test_flags"]["disable"].value_or(false)) {
        logging::notice("test disabled");
        GTEST_SKIP() << "Test disabled in configuration";
        return;
    }

    auto compilation_units = derive_compilation_units(_path, settings);
    std::vector<std::filesystem::path> object_files;

    if (!compilation_units.empty()) {
        object_files = compile_compilation_units(_path, settings, compilation_units);
    }

    std::vector<std::filesystem::path> direct_object_files = derive_object_files(_path, settings);
    object_files.insert(object_files.end(), std::make_move_iterator(direct_object_files.begin()),
                        std::make_move_iterator(direct_object_files.end()));

    auto expected_odrvs = derive_expected_odrvs(_path, settings);
    const std::vector<odrv_report> reports = orc_process(std::move(object_files));

    // Validate metrics
    bool metrics_failure = metrics_validation(settings);
    EXPECT_FALSE(metrics_failure) << "Metrics validation failed for " << _path;

    // Validate ODRV reports
    EXPECT_EQ(expected_odrvs.size(), reports.size()) << "ODRV count mismatch for " << _path;

    // Check each reported ODRV against expected ones
    for (const auto& report : reports) {
        auto found =
            std::find_if(expected_odrvs.begin(), expected_odrvs.end(),
                         [&](const auto& odrv) { return odrv_report_match(odrv, report); });
        EXPECT_NE(found, expected_odrvs.end())
            << "Unexpected ODRV found: " << report << " in " << _path;
    }
}

//--------------------------------------------------------------------------------------------------
/**
 * Creates a Google Test case for a single orc_test test.
 *
 * This function registers a new test case with Google Test framework using the
 * directory name as the test name. Any hyphens in the directory name are
 * replaced with underscores to conform to C++ identifier naming rules.
 *
 * @param home The filesystem path to the test battery directory
 *
 * @pre The path must exist and be a valid directory containing test files
 * @post A new test case is registered with Google Test framework that will
 *       create an `orc_test_instance` with the provided path when executed
 */
void create_test(const std::filesystem::path& home) {
    std::string test_name = home.stem().string();
    std::replace(test_name.begin(), test_name.end(), '-', '_');

    ::testing::RegisterTest(
        "orc_test", test_name.c_str(), nullptr, nullptr, __FILE__, __LINE__,
        [_home = home]() mutable -> ::testing::Test* { return new orc_test_instance(std::move(_home)); });
}

//--------------------------------------------------------------------------------------------------
/**
 * Recursively traverses a directory tree to find and register tests.
 *
 * This function walks through the provided directory and all its subdirectories,
 * looking for directories that contain a TOML configuration file (indicated by
 * tomlname_k). When such a directory is found, it's registered as a test.
 *
 * @param directory The filesystem path to start traversal from
 * @return The number of errors encountered during traversal
 *
 * @pre The path must exist and be a valid directory
 * @post All valid tests in the directory tree are registered with the
 *       testing framework via create_battery_test()
 */
std::size_t traverse_directory_tree(const std::filesystem::path& directory) {
    assert(is_directory(directory));

    std::size_t errors = 0;

    if (exists(directory / tomlname_k)) {
        create_test(directory);
    }

    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        try {
            if (is_directory(entry)) {
                errors += traverse_directory_tree(entry.path());
            }
        } catch (...) {
            console_error() << "\nIn battery " << entry.path() << ":";
            throw;
        }
    }

    return errors;
}

//--------------------------------------------------------------------------------------------------

} // namespace

//--------------------------------------------------------------------------------------------------

int main(int argc, char** argv) try {
    orc::profiler::initialize();

    if (argc < 2) {
        console_error() << "Usage: " << argv[0] << " /path/to/test/battery/ [--json_mode]\n";
        throw std::runtime_error("no path to test battery given");
    }

    std::filesystem::path battery_path{argv[1]};

    if (!exists(battery_path) || !is_directory(battery_path)) {
        throw std::runtime_error("test battery path is missing or not a directory");
    }

    test_settings()._json_mode = argc > 2 && std::string(argv[2]) == "--json_mode";

    // Traverse the directory tree to find and register tests,
    // adding them dynamically to the Google Test framework.
    std::size_t errors = traverse_directory_tree(battery_path);

    // Initialize and run Google Test
    ::testing::InitGoogleTest(&argc, argv);
    int gtest_result = RUN_ALL_TESTS();
    if (gtest_result != 0) {
        return gtest_result;
    }

    if (test_settings()._json_mode) {
        cout_safe([&](auto& s) { s << toml::json_formatter{toml_out()} << '\n'; });
    }

    return errors ? EXIT_FAILURE : EXIT_SUCCESS;
} catch (const std::exception& error) {
    logging::error(error.what(), "Fatal error");
    return EXIT_FAILURE;
} catch (...) {
    logging::error("unknown", "Fatal error");
    return EXIT_FAILURE;
}

//--------------------------------------------------------------------------------------------------
