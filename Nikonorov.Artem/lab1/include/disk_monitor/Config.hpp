#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace diskmon {

struct Configuration {
    std::vector<std::filesystem::path> watchDirectories;
    std::vector<std::filesystem::path> excludePaths;
    std::vector<std::string> warnings;
};

struct ConfigLoadResult {
    std::optional<Configuration> configuration;
    std::string error;
};

ConfigLoadResult loadConfiguration(const std::filesystem::path& configPath);
std::filesystem::path absoluteNormalizedPath(const std::filesystem::path& path);

} 
