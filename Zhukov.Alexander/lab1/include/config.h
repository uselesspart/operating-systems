#pragma once

#include <filesystem>
#include <vector>

struct Config {
    std::vector<std::filesystem::path> paths;
};

Config load_config(const std::filesystem::path& config_path);
