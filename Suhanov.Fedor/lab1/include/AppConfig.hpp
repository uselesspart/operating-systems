#pragma once

#include <vector>
#include <filesystem>
#include <optional>

namespace application
{

using Path = std::filesystem::path;

struct AppConfig {
    std::vector<Path> targets;
};

std::optional<AppConfig> load_config(const Path& config_path);
std::optional<AppConfig> load_config(const Path& config_path, const Path& target_base);
    
} // namespace application
