#include "disk_monitor/Config.hpp"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>
#include <unistd.h>
#include <utility>

namespace diskmon {
namespace {

std::string trim(const std::string& value) {
    const auto first = std::find_if_not(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    });
    if (first == value.end()) {
        return {};
    }
    const auto last = std::find_if_not(value.rbegin(), value.rend(), [](unsigned char ch) {
        return std::isspace(ch) != 0;
    }).base();
    return std::string(first, last);
}

std::string lineDescription(const std::filesystem::path& file, std::size_t lineNumber) {
    return file.string() + ":" + std::to_string(lineNumber);
}

std::filesystem::path resolvePath(const std::string& value,
                                  const std::filesystem::path& baseDirectory) {
    std::filesystem::path candidate(value);
    if (candidate.is_relative()) {
        candidate = baseDirectory / candidate;
    }
    return absoluteNormalizedPath(candidate);
}

}  // namespace

std::filesystem::path absoluteNormalizedPath(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::path result = path.is_absolute() ? path : std::filesystem::absolute(path, error);
    if (error) {
        result = path;
    }
    result = result.lexically_normal();

    error.clear();
    const std::filesystem::file_status finalStatus = std::filesystem::symlink_status(result, error);
    if (!error && std::filesystem::is_symlink(finalStatus)) {
        return result;
    }

    error.clear();
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(result, error);
    return error ? result : canonical;
}

ConfigLoadResult loadConfiguration(const std::filesystem::path& configPath) {
    ConfigLoadResult result;
    Configuration configuration;
    std::ifstream input(configPath);
    if (!input) {
        result.error = "cannot open configuration file '" + configPath.string() + "'";
        return result;
    }

    std::set<std::string> uniqueDirectories;
    std::set<std::string> uniqueExclusions;
    std::string rawLine;
    std::size_t lineNumber = 0;
    const std::filesystem::path baseDirectory = configPath.parent_path().empty()
        ? std::filesystem::current_path()
        : configPath.parent_path();

    while (std::getline(input, rawLine)) {
        ++lineNumber;
        if (!rawLine.empty() && rawLine.back() == '\r') {
            rawLine.pop_back();
        }
        const std::string line = trim(rawLine);
        if (line.empty() || line.front() == '#' || line.front() == ';') {
            continue;
        }

        const std::size_t separator = line.find('=');
        if (separator == std::string::npos) {
            result.error = lineDescription(configPath, lineNumber) + ": expected key=value";
            return result;
        }

        const std::string key = trim(line.substr(0, separator));
        const std::string value = trim(line.substr(separator + 1));
        if (key != "watch_dir" && key != "exclude_path") {
            result.error = lineDescription(configPath, lineNumber) + ": unknown directive '" + key + "'";
            return result;
        }
        if (value.empty()) {
            result.error = lineDescription(configPath, lineNumber) + ": " + key + " must not be empty";
            return result;
        }

        const std::filesystem::path candidate = resolvePath(value, baseDirectory);
        const std::string normalized = candidate.string();
        if (key == "exclude_path") {
            if (uniqueExclusions.insert(normalized).second) {
                configuration.excludePaths.push_back(candidate);
            } else {
                configuration.warnings.push_back(lineDescription(configPath, lineNumber) +
                                                 ": duplicate exclude_path ignored: " + normalized);
            }
            continue;
        }

        if (!uniqueDirectories.insert(normalized).second) {
            configuration.warnings.push_back(lineDescription(configPath, lineNumber) +
                                             ": duplicate directory ignored: " + normalized);
            continue;
        }

        std::error_code fsError;
        const std::filesystem::file_status rootStatus = std::filesystem::symlink_status(candidate, fsError);
        const bool isSymbolicLink = !fsError && std::filesystem::is_symlink(rootStatus);
        const bool isDirectory = !fsError && std::filesystem::is_directory(rootStatus);
        if (isSymbolicLink) {
            configuration.warnings.push_back(lineDescription(configPath, lineNumber) +
                ": watch root is a symbolic link and will not be traversed; retaining it for recovery: " + normalized);
        } else if (!isDirectory) {
            configuration.warnings.push_back(lineDescription(configPath, lineNumber) +
                ": watch root is missing, unavailable, or not a directory; retaining it for recovery: " + normalized);
        } else if (::access(normalized.c_str(), R_OK | X_OK) != 0) {
            configuration.warnings.push_back(lineDescription(configPath, lineNumber) +
                ": watch root is not currently readable/searchable; retaining it for recovery: " + normalized);
        }
        configuration.watchDirectories.push_back(candidate);
    }

    if (input.bad()) {
        result.error = "error while reading configuration file '" + configPath.string() + "'";
        return result;
    }
    if (configuration.watchDirectories.empty()) {
        result.error = "configuration contains no watch_dir entries; the current configuration is retained";
        return result;
    }

    result.configuration = std::move(configuration);
    return result;
}

}  // namespace diskmon
