#include "Config.h"

#include "Logger.h"
#include "PathUtils.h"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>

namespace diskmon {

namespace {

std::string trim(const std::string& text)
{
    constexpr const char* kSpaces = " \t\r\n";
    const auto begin = text.find_first_not_of(kSpaces);
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = text.find_last_not_of(kSpaces);
    return text.substr(begin, end - begin + 1);
}

std::optional<std::string> resolveDirectory(const std::string& entry, const std::string& baseDir,
                                            const std::string& location)
{
    const std::string path = entry.front() == '/' ? entry : paths::join(baseDir, entry);
    auto dir = paths::canonical(path);
    if (!dir) {
        const int err = errno;
        logger::warning(location + ": '" + entry + "' skipped: " + std::strerror(err));
        return std::nullopt;
    }
    if (!paths::isDirectory(*dir)) {
        logger::warning(location + ": '" + entry + "' skipped: not a directory");
        return std::nullopt;
    }
    return dir;
}

// Nested directories are already covered by recursive watching of their parent
std::vector<std::string> removeDuplicatesAndNested(std::vector<std::string> dirs)
{
    std::sort(dirs.begin(), dirs.end());

    std::vector<std::string> result;
    for (auto& dir : dirs) {
        const bool covered = std::any_of(result.begin(), result.end(), [&dir](const std::string& kept) {
            return dir == kept || paths::isInside(dir, kept);
        });
        if (covered) {
            logger::info("'" + dir + "' is already covered by another entry, skipped");
            continue;
        }
        result.push_back(std::move(dir));
    }
    return result;
}

}

std::optional<Config> Config::load(const std::string& path)
{
    std::ifstream file(path);
    if (!file) {
        logger::error("Cannot open config file " + path);
        return std::nullopt;
    }

    const std::string baseDir = paths::parent(path);
    std::vector<std::string> found;
    std::string line;
    for (int lineNumber = 1; std::getline(file, line); ++lineNumber) {
        const std::string entry = trim(line);
        if (entry.empty() || entry.front() == '#') {
            continue;
        }
        const std::string location = path + ":" + std::to_string(lineNumber);
        if (auto dir = resolveDirectory(entry, baseDir, location)) {
            found.push_back(std::move(*dir));
        }
    }
    return Config{removeDuplicatesAndNested(std::move(found))};
}

}
