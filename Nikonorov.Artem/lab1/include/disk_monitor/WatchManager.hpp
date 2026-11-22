#pragma once

#include <cstdint>
#include <filesystem>
#include <set>
#include <string>
#include <sys/inotify.h>
#include <unordered_map>
#include <vector>

namespace diskmon {

class WatchManager {
public:
    WatchManager();
    WatchManager(const WatchManager&) = delete;
    WatchManager& operator=(const WatchManager&) = delete;
    ~WatchManager();

    bool initialize();
    bool applyRoots(const std::vector<std::filesystem::path>& roots,
                    const std::vector<std::filesystem::path>& excludePaths = {});
    bool reconcile();
    void processAvailableEvents();
    int descriptor() const noexcept;

private:
    bool scanDirectory(const std::filesystem::path& directory,
                       std::set<std::string>& desired,
                       bool isConfiguredRoot,
                       bool discovered,
                       std::unordered_map<std::string, std::string>& rootAnchors);
    bool addRootAnchor(const std::filesystem::path& root,
                       std::set<std::string>& desired,
                       std::unordered_map<std::string, std::string>& rootAnchors);
    bool scanNewDirectory(const std::filesystem::path& directory);
    bool ensureWatch(const std::filesystem::path& directory);
    void removeStaleWatches(const std::set<std::string>& desired);
    void rebaseWatches(const std::filesystem::path& oldPath,
                       const std::filesystem::path& newPath);
    bool processEvent(const struct inotify_event& event);
    bool isAnchorOnlyPath(const std::filesystem::path& path) const;
    bool anchorEventMatchesRoot(const std::filesystem::path& anchor,
                                const std::string& name,
                                std::uint32_t mask) const;
    void logDiscoveredObject(const std::filesystem::path& path, bool isDirectory);
    void logScanErrorOnce(const std::string& message);
    bool isLogSinkPath(const std::filesystem::path& path) const;

    int descriptor_ = -1;
    std::vector<std::filesystem::path> roots_;
    std::vector<std::filesystem::path> excludePaths_;
    std::unordered_map<int, std::string> watchToPath_;
    std::unordered_map<std::string, int> pathToWatch_;
    std::set<int> pendingIgnored_;
    std::set<std::string> alreadyReportedErrors_;
    std::unordered_map<std::uint32_t, std::filesystem::path> pendingDirectoryMoves_;
    std::unordered_map<std::string, std::string> rootAnchorByRoot_;
};

}  
