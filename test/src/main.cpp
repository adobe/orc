// stdc++
#include <exception>
#include <filesystem>
#include <iostream>
#include <array>

// posix
#include <unistd.h>
#include <fcntl.h>

// toml++
#include <toml++/toml.h>

// orc
#include <orc/orc.hpp>

/**************************************************************************************************/

namespace {

/**************************************************************************************************/

void assume(bool condition, std::string message) {
    if (condition) return;
    throw std::runtime_error(message);
}

auto temp_file_path(const std::filesystem::path& battery_path,
                    const std::filesystem::path& src) {
    auto result = std::filesystem::temp_directory_path() / "orc_test" / battery_path.filename() / (src.stem().string() + ".obj");
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

struct compilation_unit {
    std::filesystem::path _src;
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

struct expected_odrv {
    std::string _category;
    std::string _linkage_name;
};

/**************************************************************************************************/

const char* to_string(toml::node_type x) {
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
                                                       const toml::table& table) {
    std::vector<compilation_unit> result;
    const toml::array* arr = table["source"].as_array();
    if (!arr) return result;
    for (const toml::node& src_node : *arr) {
        const toml::table* src_ptr = src_node.as_table();
        if (!src_ptr) {
            throw std::runtime_error(std::string("expected a source table, found: ") + to_string(src_node.type()));
        }
        const toml::table& src = *src_ptr;
        compilation_unit unit;
        std::optional<std::string_view> name = src["name"].value<std::string_view>();
        if (!name) {
            throw std::runtime_error("Missing required source key \"name\"");
        }
        unit._src = home / *name;

        // REVISIT (fosterbrereton): Do things here as necessary for compiler flags

        validate_compilation_unit(unit);
        result.push_back(unit);
    }
    return result;
}

/**************************************************************************************************/

std::vector<expected_odrv> derive_expected_odrvs(const std::filesystem::path& home,
                                                 const toml::table& table) {
    std::vector<expected_odrv> result;
    const toml::array* arr = table["odrv"].as_array();
    if (!arr) return result;
    for (const toml::node& odrv_node : *arr) {
        const toml::table* odrv_ptr = odrv_node.as_table();
        if (!odrv_ptr) {
            throw std::runtime_error(std::string("expected a source table, found: ") + to_string(odrv_node.type()));
        }
        const toml::table& src = *odrv_ptr;
        expected_odrv odrv;

        std::optional<std::string_view> category = src["category"].value<std::string_view>();
        if (!category) {
            throw std::runtime_error("Missing required odrv key \"category\"");
        }
        odrv._category = *category;

        std::optional<std::string_view> linkage_name = src["linkage_name"].value<std::string_view>();
        if (!linkage_name) {
            throw std::runtime_error("Missing required odrv key \"linkage_name\"");
        }
        odrv._linkage_name = *linkage_name;

        result.push_back(odrv);
    }
    return result;
}

/**************************************************************************************************/

void run_battery_test(const std::filesystem::path& home) {
    assume(is_directory(home), "\"" + home.string() + "\" is not a directory");
    std::string tomlname = "odrv_test.toml";
    std::filesystem::path tomlpath = home / tomlname;
    assume(is_regular_file(tomlpath), "\"" + tomlpath.string() + "\" is not a regular file");
    toml::table settings;

    std::cout << "Testing " << home << "...\n";

    try {
        settings = toml::parse_file(tomlpath.string());
    } catch (const toml::parse_error& error) {
        std::cerr << error << '\n';
        throw std::runtime_error("settings file parsing error");
    }

    // Save this for debugging purposes.
    // std::cerr << toml::json_formatter{settings} << '\n';

    const bool preserve_object_files = settings["orc_test_flags"]["preserve_object_files"].value_or(false);

    auto compilation_units = derive_compilation_units(home, settings);
    if (compilation_units.empty()) {
        throw std::runtime_error("found no sources to compile");
    }

    auto expected_odrvs = derive_expected_odrvs(home, settings);
    if (expected_odrvs.empty()) {
        throw std::runtime_error("found no expected ODRVs");
    }

    std::cout << "Compiling " << compilation_units.size() << " source file(s)\n";
    std::vector<std::filesystem::path> object_files;
    for (auto& unit : compilation_units) {
        auto temp_path = temp_file_path(home, unit._src);
        if (!preserve_object_files) {
            unit._path = temp_path;
        }
        std::string command(path_to_clang() + " -g -c " + unit._src.string() + " -o " + temp_path.string());
        // Save this for debugging purposes.
        // std::cout << command << '\n';
        std::string result = exec(command.c_str());
        if (!result.empty()) {
            std::cout << result;
        }
        object_files.push_back(temp_path);
    }

    orc_reset();
    auto reports = orc_process(object_files);

    // At this point, the reports.size() should match the expected_odrvs.size()
    bool unexpected_result = false;
    if (expected_odrvs.size() != reports.size()) {
        unexpected_result = true;
    } else {
        for (const auto& report : reports) {
            auto found = std::find_if(expected_odrvs.begin(), expected_odrvs.end(), [&](const auto& odrv){
                return odrv._category == report.category();// &&
                       //odrv._linkage_name == report.linkage_name;
            });

            if (found == expected_odrvs.end()) {
                unexpected_result = true;
                break;
            }

            std::cout << "Found expected " << report.category() << " ODRV\n";
        }
        // go through the reports and make sure everything matches up.
    }

    if (unexpected_result) {
        // If there's an error in the test, dump what we've found to assist debugging.
        for (const auto& report : reports) {
            std::cout << report << '\n';
        }

        if (reports.empty()) {
            std::cout << "0 ODRVs reported.\n";
        }
    }
}

/**************************************************************************************************/

} // namespace

/**************************************************************************************************/

int main(int argc, char** argv) try {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " /path/to/test/battery/\n";
        throw std::runtime_error("no path to test battery given");
    }

    std::filesystem::path battery_path{argv[1]};

    if (!exists(battery_path) || !is_directory(battery_path)) {
        throw std::runtime_error("test battery path is missing or not a directory");
    }

    for (const auto& path : std::filesystem::directory_iterator(battery_path)) {
        try {
            run_battery_test(path);
        } catch (...) {
            std::cerr << "In battery " << path.path() << ":\n";
            throw;
        }
    }

    return EXIT_SUCCESS;
} catch (const std::exception& error) {
    std::cerr << "Fatal error: " << error.what() << '\n';
    return EXIT_FAILURE;
} catch (...) {
    std::cerr << "Fatal error: unknown\n";
    return EXIT_FAILURE;
}

/**************************************************************************************************/
