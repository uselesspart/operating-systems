#pragma once

#include <filesystem>
#include <vector>

namespace disk_monitor {

struct Config {
    std::filesystem::path file;
    std::vector<std::filesystem::path> directories;
};

class ConfigParser {
public:
    static std::filesystem::path absoluteConfigPath(const std::filesystem::path& path);
    static Config load(const std::filesystem::path& path);
};

}  // namespace disk_monitor

