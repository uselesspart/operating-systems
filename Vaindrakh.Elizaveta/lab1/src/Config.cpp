#include "Config.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string>

namespace disk_monitor {
namespace {

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::filesystem::path normalizeDirectory(const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path.lexically_normal() : canonical;
}

}  // namespace

std::filesystem::path ConfigParser::absoluteConfigPath(const std::filesystem::path& path) {
    std::error_code error;
    const auto absolute = std::filesystem::absolute(path, error);
    if (error) {
        throw std::runtime_error("cannot resolve configuration path '" + path.string() + "'");
    }
    return absolute.lexically_normal();
}

Config ConfigParser::load(const std::filesystem::path& path) {
    const auto absolute_path = absoluteConfigPath(path);
    std::ifstream input(absolute_path);
    if (!input) {
        throw std::runtime_error("cannot open configuration file '" + absolute_path.string() + "'");
    }

    Config result;
    result.file = absolute_path;
    const auto config_directory = absolute_path.parent_path();
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const auto comment = line.find('#');
        if (comment != std::string::npos) {
            line.erase(comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }
        if (line.rfind("DIR=", 0) == 0) {
            line = trim(line.substr(4));
        }
        if (line.empty()) {
            continue;
        }

        std::filesystem::path directory(line);
        if (directory.is_relative()) {
            directory = config_directory / directory;
        }
        directory = normalizeDirectory(directory);
        if (std::find(result.directories.begin(), result.directories.end(), directory) ==
            result.directories.end()) {
            result.directories.push_back(directory);
        }
    }

    return result;
}

}  // namespace disk_monitor

