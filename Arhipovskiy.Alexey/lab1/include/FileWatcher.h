#ifndef DISKMON_FILE_WATCHER_H
#define DISKMON_FILE_WATCHER_H

#include "UniqueFd.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <sys/inotify.h>

namespace diskmon {

// Recursive inotify watcher
class FileWatcher {
public:
    FileWatcher() = default;

    FileWatcher(const FileWatcher&) = delete;
    FileWatcher& operator=(const FileWatcher&) = delete;

    [[nodiscard]] bool start(const std::vector<std::string>& roots);
    void stop();
    void processEvents();

    [[nodiscard]] int fd() const noexcept { return fd_.get(); }

private:
    struct PendingMove {
        std::uint32_t cookie;
        std::string path;
    };

    void handleEvent(const inotify_event& event);
    [[nodiscard]] std::optional<std::string> resolvePendingMove(const inotify_event& event);
    [[nodiscard]] bool consumeOwnScanEvent(const inotify_event& event, const std::string& path);
    void trackDirectory(const inotify_event& event, const std::string& path,
                        const std::optional<std::string>& renamedFrom);

    void watchTree(const std::string& root, bool parentWatched);
    void unwatchTree(const std::string& root);
    void renameTree(const std::string& from, const std::string& to);
    [[nodiscard]] bool isRoot(const std::string& dir) const;

    UniqueFd fd_;
    std::unordered_map<int, std::string> watches_;
    std::unordered_set<std::string> roots_;
    std::unordered_set<std::string> ownScans_;
    std::optional<PendingMove> pendingMove_;
};

}

#endif // DISKMON_FILE_WATCHER_H
