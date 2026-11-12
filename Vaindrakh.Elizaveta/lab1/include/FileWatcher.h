#pragma once

#include "Logger.h"
#include "UniqueFd.h"

#include <sys/inotify.h>

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace disk_monitor {

class FileWatcher {
public:
    explicit FileWatcher(Logger& logger);
    ~FileWatcher() = default;

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    int descriptor() const noexcept;
    void reload(const std::vector<std::filesystem::path>& roots);
    void consumeEvents();

private:
    struct Watch {
        std::filesystem::path directory;
    };

    void removeAllWatches();
    void addDirectoryTree(const std::filesystem::path& directory);
    void addDirectory(const std::filesystem::path& directory);
    void removeDirectoryTree(const std::filesystem::path& directory);
    bool isWatched(const std::filesystem::path& directory) const;
    bool isCoveredByExistingWatch(const std::filesystem::path& directory) const;
    void logEvent(const inotify_event& event, const std::filesystem::path& path) const;

    Logger& logger_;
    UniqueFd inotify_fd_;
    std::unordered_map<int, Watch> watches_;
    std::unordered_map<std::string, int> directory_to_watch_;
};

}  // namespace disk_monitor

