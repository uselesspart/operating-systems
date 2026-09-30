#ifndef DISKMON_PATH_UTILS_H
#define DISKMON_PATH_UTILS_H

#include <optional>
#include <string>

namespace diskmon::paths {

// Resolves symlinks, "." and ".."; on failure returns nullopt and keeps errno
[[nodiscard]] std::optional<std::string> canonical(const std::string& path);

[[nodiscard]] bool isDirectory(const std::string& path);
[[nodiscard]] bool isInside(const std::string& path, const std::string& ancestor);
[[nodiscard]] std::string join(const std::string& dir, const std::string& name);
[[nodiscard]] std::string parent(const std::string& path);

}

#endif // DISKMON_PATH_UTILS_H
