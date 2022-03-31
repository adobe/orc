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

namespace orc_test {

/**************************************************************************************************/

void assume(bool condition, std::string message) {
    if (condition) return;
    throw std::runtime_error(message);
}

void posix_assume(int err, std::string message) {
    if (err != -1) return;
    throw std::runtime_error("POSIX: " + message + " (" + std::to_string(errno) + ")");
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

std::vector<compilation_unit> derive_compilation_units(const std::filesystem::path& home,
                                                       const toml::table& table) {
    std::vector<compilation_unit> result;
    const toml::array* arr = table["sources"].as_array();
    if (!arr) return result;
    for (const toml::node& elem : *arr) {
        if (std::optional<std::string_view> str = elem.value<std::string_view>()) {
            compilation_unit unit;
            unit._src = home / *str;
            result.push_back(std::move(unit));
            // REVISIT (fosterbrereton): Do things here as necessary for compiler flags
        }
    }
    return result;
}

/**************************************************************************************************/

void run_battery_test(const std::filesystem::path& home) {
    assume(is_directory(home), "\"" + home.string() + "\" is not a directory");
    std::string tomlname = "odrv_test.toml";
    std::filesystem::path tomlpath = home / tomlname;
    assume(is_regular_file(tomlpath), "\"" + tomlpath.string() + "\" is not a regular file");
    // REVISIT (fosterbrereton): toml++ has extended information within the toml::parse_error
    // class, that we would do well to surface here.
    toml::table settings = toml::parse_file(tomlpath.string());

    const bool preserve_object_files = settings["orc_test_flags"]["preserve_object_files"].value_or(false);

    auto compilation_units = derive_compilation_units(home, settings);
    if (compilation_units.empty()) {
        throw std::runtime_error("found no sources to compile");
    }
    std::cout << "Compiling " << compilation_units.size() << " source file(s)\n";
    std::vector<std::filesystem::path> object_files;
    for (auto& unit : compilation_units) {
        auto temp_path = temp_file_path(home, unit._src);
        if (!preserve_object_files) {
            unit._path = temp_path;
        }
        std::string command(path_to_clang() + " -g -c " + unit._src.string() + " -o " + temp_path.string());
        // std::cout << command << '\n';
        std::string result = exec(command.c_str());
        if (!result.empty()) {
            std::cout << result;
        }
        object_files.push_back(temp_path);
    }
    orc_process(object_files);
}

/**************************************************************************************************/

} // namespace orc_test

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
            orc_test::run_battery_test(path);
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
