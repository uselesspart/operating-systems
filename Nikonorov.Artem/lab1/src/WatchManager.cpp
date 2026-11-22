#include "disk_monitor/WatchManager.hpp"

#include "disk_monitor/Config.hpp"
#include "disk_monitor/Logging.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <string>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace diskmon {
namespace {

constexpr std::uint32_t kWatchMask = IN_ALL_EVENTS | IN_ONLYDIR | IN_DONT_FOLLOW;
constexpr std::array<const char*, 5> kKnownSystemLogFiles{{
    "/var/log/disk_monitor.log",
    "/var/log/syslog",
    "/var/log/messages",
    "/var/log/daemon.log",
    "/var/log/user.log",
}};
constexpr std::array<const char*, 2> kJournalDirectories{{
    "/var/log/journal",
    "/run/log/journal",
}};
bool pathHasPrefix(const std::string& candidate, const std::string& base) {
    return candidate == base ||
           (candidate.size() > base.size() &&
            candidate.compare(0, base.size(), base) == 0 && candidate[base.size()] == '/');
}

bool matchesFileOrRotation(const std::string& candidate, const std::string& base) {
    return candidate == base ||
           (candidate.size() > base.size() &&
            candidate.compare(0, base.size(), base) == 0 &&
            (candidate[base.size()] == '.' || candidate[base.size()] == '-'));
}

std::string sanitizePath(const std::filesystem::path& path) {
    return sanitizeLogField(path.string());
}

}  // namespace

WatchManager::WatchManager() = default;

WatchManager::~WatchManager() {
    if (descriptor_ >= 0) {
        (void)::close(descriptor_);
    }
}

bool WatchManager::initialize() {
    descriptor_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (descriptor_ < 0) {
        logDaemon(LOG_ERR, "inotify_init1 failed: " + std::string(std::strerror(errno)));
        return false;
    }
    return true;
}

bool WatchManager::applyRoots(const std::vector<std::filesystem::path>& roots,
                              const std::vector<std::filesystem::path>& excludePaths) {
    if (descriptor_ < 0) {
        logDaemon(LOG_ERR, "cannot apply watch roots before inotify initialization");
        return false;
    }

    const std::vector<std::filesystem::path> oldRoots = roots_;
    const std::vector<std::filesystem::path> oldExclusions = excludePaths_;
    roots_ = roots;
    excludePaths_.clear();
    for (const auto& path : excludePaths) {
        excludePaths_.push_back(absoluteNormalizedPath(path));
    }

    std::set<std::string> desired;
    std::unordered_map<std::string, std::string> newRootAnchors;
    bool allRootsRecoverable = true;
    for (const auto& root : roots_) {
        const std::string key = absoluteNormalizedPath(root).string();
        const bool wasUnavailable = rootAnchorByRoot_.find(key) != rootAnchorByRoot_.end();
        if (!scanDirectory(root, desired, true, wasUnavailable, newRootAnchors)) {
            allRootsRecoverable = false;
            logScanErrorOnce("cannot watch configured root or any parent anchor for '" + root.string() +
                             "'; configuration was not replaced");
        }
    }

    if (!allRootsRecoverable) {
        roots_ = oldRoots;
        excludePaths_ = oldExclusions;
        (void)reconcile();
        return false;
    }

    removeStaleWatches(desired);
    rootAnchorByRoot_ = std::move(newRootAnchors);
    return true;
}

bool WatchManager::reconcile() {
    if (descriptor_ < 0) {
        return false;
    }
    std::set<std::string> desired;
    std::unordered_map<std::string, std::string> newRootAnchors;
    bool allRootsRecoverable = true;
    for (const auto& root : roots_) {
        const std::string key = absoluteNormalizedPath(root).string();
        const bool wasUnavailable = rootAnchorByRoot_.find(key) != rootAnchorByRoot_.end();
        if (!scanDirectory(root, desired, true, wasUnavailable, newRootAnchors)) {
            allRootsRecoverable = false;
            logScanErrorOnce("cannot currently watch configured root or any parent anchor for '" + root.string() + "'");
        }
    }

    removeStaleWatches(desired);
    rootAnchorByRoot_ = std::move(newRootAnchors);
    return allRootsRecoverable;
}

bool WatchManager::scanDirectory(const std::filesystem::path& directory,
                                std::set<std::string>& desired,
                                bool isConfiguredRoot,
                                bool discovered,
                                std::unordered_map<std::string, std::string>& rootAnchors) {
    const std::filesystem::path normalized = absoluteNormalizedPath(directory);
    if (isLogSinkPath(normalized)) {
        if (isConfiguredRoot) {
            logScanErrorOnce("configured root is excluded from monitoring: '" + normalized.string() + "'");
            return false;
        }
        return true;
    }

    std::error_code fsError;
    const std::filesystem::file_status status = std::filesystem::symlink_status(normalized, fsError);
    if (fsError || !std::filesystem::is_directory(status)) {
        if (isConfiguredRoot) {
            logScanErrorOnce("configured root is missing, unavailable, or not a directory; waiting for it: '" +
                             normalized.string() + "'");
            return addRootAnchor(normalized, desired, rootAnchors);
        }
        return true;
    }

    if (!ensureWatch(normalized)) {
        if (isConfiguredRoot) {
            logScanErrorOnce("cannot watch configured root; watching a parent for replacement/recovery: '" +
                             normalized.string() + "'");
            return addRootAnchor(normalized, desired, rootAnchors);
        }
        return true;
    }
    desired.insert(normalized.string());

    std::filesystem::directory_iterator iterator(normalized, fsError);
    if (fsError) {
        logScanErrorOnce("cannot list directory '" + normalized.string() + "': " + fsError.message());
        return true;
    }

    const std::filesystem::directory_iterator end;
    while (iterator != end) {
        const std::filesystem::directory_entry entry = *iterator;
        const std::filesystem::path entryPath = absoluteNormalizedPath(entry.path());
        if (!isLogSinkPath(entryPath)) {
            std::error_code entryError;
            const auto type = entry.symlink_status(entryError);
            if (!entryError) {
                const bool isDirectory = std::filesystem::is_directory(type);
                if (discovered) {
                    logDiscoveredObject(entryPath, isDirectory);
                }
                if (isDirectory) {
                    (void)scanDirectory(entryPath, desired, false, discovered, rootAnchors);
                }
            } else {
                logScanErrorOnce("cannot inspect path '" + entryPath.string() + "': " + entryError.message());
            }
        }

        iterator.increment(fsError);
        if (fsError) {
            logScanErrorOnce("directory iteration failed for '" + normalized.string() + "': " + fsError.message());
            fsError.clear();
        }
    }
    return true;
}

bool WatchManager::addRootAnchor(const std::filesystem::path& root,
                                 std::set<std::string>& desired,
                                 std::unordered_map<std::string, std::string>& rootAnchors) {
    const std::filesystem::path normalizedRoot = absoluteNormalizedPath(root);
    const std::string rootKey = normalizedRoot.string();
    std::filesystem::path candidate = normalizedRoot.parent_path();
    if (candidate.empty()) {
        candidate = normalizedRoot.root_path();
    }

    while (!candidate.empty()) {
        candidate = candidate.lexically_normal();
        std::error_code fsError;
        const auto status = std::filesystem::symlink_status(candidate, fsError);
        if (!fsError && std::filesystem::is_directory(status) && !isLogSinkPath(candidate)) {
            if (ensureWatch(candidate)) {
                const std::string anchor = absoluteNormalizedPath(candidate).string();
                desired.insert(anchor);
                rootAnchors[rootKey] = anchor;
                logScanErrorOnce("configured root is not currently watched; recovery anchor='" + anchor +
                                 "' root='" + rootKey + "'");
                return true;
            }
        }
        const std::filesystem::path parent = candidate.parent_path();
        if (parent.empty() || parent == candidate) {
            break;
        }
        candidate = parent;
    }

    logScanErrorOnce("no watchable existing parent was found for configured root '" + rootKey + "'");
    return false;
}

bool WatchManager::scanNewDirectory(const std::filesystem::path& directory) {
    std::set<std::string> desired;
    std::unordered_map<std::string, std::string> anchors;
    return scanDirectory(directory, desired, false, true, anchors);
}

bool WatchManager::ensureWatch(const std::filesystem::path& directory) {
    const std::string path = absoluteNormalizedPath(directory).string();
    const int watchDescriptor = ::inotify_add_watch(descriptor_, path.c_str(), kWatchMask);
    if (watchDescriptor < 0) {
        logScanErrorOnce("inotify_add_watch failed for '" + path + "': " + std::strerror(errno));
        return false;
    }

    const auto oldPath = watchToPath_.find(watchDescriptor);
    if (oldPath != watchToPath_.end() && oldPath->second != path) {
        const auto oldPathMapping = pathToWatch_.find(oldPath->second);
        if (oldPathMapping != pathToWatch_.end() && oldPathMapping->second == watchDescriptor) {
            pathToWatch_.erase(oldPathMapping);
        }
        oldPath->second = path;
    } else if (oldPath == watchToPath_.end()) {
        watchToPath_.emplace(watchDescriptor, path);
    }
    pathToWatch_[path] = watchDescriptor;
    return true;
}

void WatchManager::removeStaleWatches(const std::set<std::string>& desired) {
    std::vector<std::pair<int, std::string>> stale;
    for (const auto& entry : watchToPath_) {
        const auto reverse = pathToWatch_.find(entry.second);
        if (desired.find(entry.second) == desired.end() ||
            reverse == pathToWatch_.end() || reverse->second != entry.first) {
            stale.emplace_back(entry.first, entry.second);
        }
    }

    for (const auto& entry : stale) {
        const int watchDescriptor = entry.first;
        const std::string& path = entry.second;
        if (::inotify_rm_watch(descriptor_, watchDescriptor) == 0) {
            pendingIgnored_.insert(watchDescriptor);
        } else if (errno != EINVAL) {
            logScanErrorOnce("inotify_rm_watch failed for '" + path + "': " + std::strerror(errno));
        }
        const auto pathMapping = pathToWatch_.find(path);
        if (pathMapping != pathToWatch_.end() && pathMapping->second == watchDescriptor) {
            pathToWatch_.erase(pathMapping);
        }
        const auto watchMapping = watchToPath_.find(watchDescriptor);
        if (watchMapping != watchToPath_.end() && watchMapping->second == path) {
            watchToPath_.erase(watchMapping);
        }
    }
}

void WatchManager::rebaseWatches(const std::filesystem::path& oldPath,
                                 const std::filesystem::path& newPath) {
    const std::string oldPrefix = absoluteNormalizedPath(oldPath).string();
    const std::string newPrefix = absoluteNormalizedPath(newPath).string();
    std::vector<std::pair<int, std::string>> updates;
    for (const auto& entry : watchToPath_) {
        if (pathHasPrefix(entry.second, oldPrefix)) {
            std::string replacement = newPrefix;
            if (entry.second.size() > oldPrefix.size()) {
                replacement += entry.second.substr(oldPrefix.size());
            }
            updates.emplace_back(entry.first, replacement);
        }
    }

    for (const auto& update : updates) {
        const auto previous = watchToPath_.find(update.first);
        if (previous != watchToPath_.end()) {
            const auto oldReverse = pathToWatch_.find(previous->second);
            if (oldReverse != pathToWatch_.end() && oldReverse->second == update.first) {
                pathToWatch_.erase(oldReverse);
            }
            previous->second = update.second;
            pathToWatch_[update.second] = update.first;
        }
    }
}

bool WatchManager::isAnchorOnlyPath(const std::filesystem::path& path) const {
    const std::string candidate = absoluteNormalizedPath(path).string();
    bool isAnchor = false;
    for (const auto& entry : rootAnchorByRoot_) {
        if (entry.second == candidate) {
            isAnchor = true;
            break;
        }
    }
    if (!isAnchor) {
        return false;
    }
    for (const auto& root : roots_) {
        const std::string rootPath = absoluteNormalizedPath(root).string();
        if (pathHasPrefix(candidate, rootPath) && pathToWatch_.find(rootPath) != pathToWatch_.end()) {
            return false;
        }
    }
    return true;
}

bool WatchManager::anchorEventMatchesRoot(const std::filesystem::path& anchor,
                                           const std::string& name,
                                           std::uint32_t mask) const {
    constexpr std::uint32_t changes = IN_CREATE | IN_MOVED_TO | IN_DELETE | IN_MOVED_FROM | IN_ATTRIB;
    if ((mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED | IN_UNMOUNT | IN_Q_OVERFLOW)) != 0U) {
        return true;
    }
    if (name.empty() || (mask & changes) == 0U) {
        return false;
    }

    const std::string anchorPath = absoluteNormalizedPath(anchor).string();
    for (const auto& entry : rootAnchorByRoot_) {
        if (entry.second != anchorPath) {
            continue;
        }
        const std::filesystem::path target(entry.first);
        const std::filesystem::path base(anchorPath);
        const std::filesystem::path relative = target.lexically_relative(base);
        if (relative.empty() || relative == "." || relative.is_absolute()) {
            continue;
        }
        const auto first = relative.begin();
        if (first != relative.end() && first->string() == name) {
            return true;
        }
    }
    return false;
}

void WatchManager::logDiscoveredObject(const std::filesystem::path& path, bool isDirectory) {
    if (isLogSinkPath(path)) {
        return;
    }
    std::ostringstream message;
    message << "time=" << currentTimestamp()
            << " source=synthetic"
            << " event=DISCOVERED"
            << " object_type=" << (isDirectory ? "directory" : "file")
            << " directory=" << sanitizePath(path.parent_path())
            << " name=" << sanitizeLogField(path.filename().string())
            << " path=" << sanitizePath(path);
    logFilesystemEvent(message.str());
}

void WatchManager::logScanErrorOnce(const std::string& message) {
    if (alreadyReportedErrors_.insert(message).second) {
        logDaemon(LOG_WARNING, message);
    }
}

bool WatchManager::isLogSinkPath(const std::filesystem::path& path) const {
    const std::filesystem::path normalizedPath = absoluteNormalizedPath(path);
    const std::string candidate = normalizedPath.string();

    for (const char* journalPath : kJournalDirectories) {
        if (pathHasPrefix(candidate, journalPath)) {
            return true;
        }
    }

    std::vector<std::filesystem::path> exclusions;
    exclusions.reserve(kKnownSystemLogFiles.size() + excludePaths_.size());
    for (const char* knownLog : kKnownSystemLogFiles) {
        exclusions.emplace_back(knownLog);
    }
    exclusions.insert(exclusions.end(), excludePaths_.begin(), excludePaths_.end());

    struct stat candidateMetadata {};
    const bool candidateExists = ::stat(candidate.c_str(), &candidateMetadata) == 0;
    for (const auto& excludedPath : exclusions) {
        const std::filesystem::path normalizedExclude = absoluteNormalizedPath(excludedPath);
        const std::string base = normalizedExclude.string();
        if (matchesFileOrRotation(candidate, base)) {
            return true;
        }

        struct stat excludeMetadata {};
        if (::stat(base.c_str(), &excludeMetadata) == 0) {
            if (S_ISDIR(excludeMetadata.st_mode) && pathHasPrefix(candidate, base)) {
                return true;
            }
            if (candidateExists && candidateMetadata.st_dev == excludeMetadata.st_dev &&
                candidateMetadata.st_ino == excludeMetadata.st_ino) {
                return true;
            }
        }
    }
    return false;
}

int WatchManager::descriptor() const noexcept {
    return descriptor_;
}

}  // namespace diskmon
