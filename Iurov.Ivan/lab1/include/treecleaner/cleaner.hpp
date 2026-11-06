#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace treecleaner {

struct CleanResult {
    std::vector<std::filesystem::path> removedDirectories;
    std::vector<std::string> errors;
};

// Deletes directories exactly `depth` levels below `root`, including their
// contents. The root and any shallower directories are preserved. Symlinks
// are never followed or treated as directories. `depth` must be >= 1.
CleanResult cleanAtDepth(const std::filesystem::path& root, std::size_t depth);

} // namespace treecleaner
