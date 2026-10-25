#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct ConfigEntry {
    std::filesystem::path folder;
    int depth;
};

class Config {
public:
    static Config load(const std::filesystem::path& config_path);

    const std::vector<ConfigEntry>& entries() const noexcept;

private:
    std::vector<ConfigEntry> entries_;
};