#pragma once

#include <cstddef>
#include <filesystem>
#include <vector>

namespace treecleaner {

struct ConfigEntry {
    std::filesystem::path folder;
    std::size_t depth{};
};

// Reads lines in the form "folder depth". Relative paths are resolved from
// the directory containing the configuration file. Throws std::runtime_error
// with a line number when a non-comment line is invalid.
std::vector<ConfigEntry> loadConfig(const std::filesystem::path& configPath);

} // namespace treecleaner
