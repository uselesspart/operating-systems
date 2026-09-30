#include "FileWatcher.h"

#include "Logger.h"
#include "PathUtils.h"

#include <cerrno>
#include <cstring>
#include <memory>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

namespace diskmon {

namespace {

constexpr std::uint32_t kWatchMask = IN_ALL_EVENTS | IN_ONLYDIR;
constexpr std::uint32_t kScanEvents = IN_OPEN | IN_ACCESS | IN_CLOSE_NOWRITE;
constexpr std::uint32_t kRootGoneEvents = IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT;
constexpr std::size_t kEventBufferSize = std::size_t{64} * 1024;
constexpr std::size_t kEventNameWidth = 13;

struct EventName {
    std::uint32_t mask;
    const char* name;
};

constexpr EventName kEventNames[] = {
    {IN_ACCESS, "ACCESS"},     {IN_MODIFY, "MODIFY"},           {IN_ATTRIB, "ATTRIB"},
    {IN_OPEN, "OPEN"},         {IN_CLOSE_WRITE, "CLOSE_WRITE"}, {IN_CLOSE_NOWRITE, "CLOSE_NOWRITE"},
    {IN_CREATE, "CREATE"},     {IN_DELETE, "DELETE"},           {IN_MOVED_FROM, "MOVED_FROM"},
    {IN_MOVED_TO, "MOVED_TO"}, {IN_DELETE_SELF, "DELETE_SELF"}, {IN_MOVE_SELF, "MOVE_SELF"},
    {IN_UNMOUNT, "UNMOUNT"},
};

struct DirCloser {
    void operator()(DIR* dir) const noexcept { closedir(dir); }
};

using DirHandle = std::unique_ptr<DIR, DirCloser>;

std::string describeMask(std::uint32_t mask)
{
    std::string result;
    for (const EventName& event : kEventNames) {
        if (mask & event.mask) {
            if (!result.empty()) {
                result += '|';
            }
            result += event.name;
        }
    }
    return result.empty() ? "UNKNOWN" : result;
}

void logEvent(const inotify_event& event, const std::string& path)
{
    std::string message = describeMask(event.mask);
    if (message.size() < kEventNameWidth) {
        message.resize(kEventNameWidth, ' ');
    }
    message += (event.mask & IN_ISDIR) ? " dir  " : " file ";
    message += path;
    if (event.mask & (IN_MOVED_FROM | IN_MOVED_TO)) {
        message += " (cookie " + std::to_string(event.cookie) + ")";
    }
    logger::event(message);
}

// Symlinks are not followed to avoid cycles
bool isRealDirectory(const dirent& entry, const std::string& path)
{
    if (entry.d_type != DT_UNKNOWN) {
        return entry.d_type == DT_DIR;
    }
    struct stat info {};
    return lstat(path.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

// Returns false only if the root itself cannot be opened; errno is preserved.
bool collectSubtree(const std::string& root, std::vector<std::string>& out)
{
    std::vector<std::string> pending{root};
    while (!pending.empty()) {
        std::string dir = std::move(pending.back());
        pending.pop_back();

        const DirHandle handle(opendir(dir.c_str()));
        if (!handle) {
            if (dir == root) {
                return false;
            }
            continue;
        }
        while (const dirent* entry = readdir(handle.get())) {
            const std::string name = entry->d_name;
            if (name == "." || name == "..") {
                continue;
            }
            std::string child = paths::join(dir, name);
            if (isRealDirectory(*entry, child)) {
                pending.push_back(std::move(child));
            }
        }
        out.push_back(std::move(dir));
    }
    return true;
}

}

bool FileWatcher::start(const std::vector<std::string>& roots)
{
    stop();

    fd_.reset(inotify_init1(IN_NONBLOCK | IN_CLOEXEC));
    if (!fd_.valid()) {
        const int err = errno;
        logger::error(std::string("inotify_init1 failed: ") + std::strerror(err));
        return false;
    }

    for (const auto& root : roots) {
        roots_.insert(root);
        const std::size_t before = watches_.size();
        watchTree(root, false);
        logger::info("Watching " + root + ": " + std::to_string(watches_.size() - before)
                     + " dir(s) including subdirectories");
    }

    logger::event("=== monitoring started: " + std::to_string(watches_.size()) + " directories in "
                  + std::to_string(roots_.size()) + " tree(s) ===");
    return true;
}

void FileWatcher::stop()
{
    if (fd_.valid()) {
        fd_.reset();
        logger::event("=== monitoring stopped ===");
    }
    watches_.clear();
    roots_.clear();
    ownScans_.clear();
    pendingMove_.reset();
}

void FileWatcher::processEvents()
{
    alignas(inotify_event) char buffer[kEventBufferSize];

    while (fd_.valid()) {
        const ssize_t length = read(fd_.get(), buffer, sizeof(buffer));
        if (length < 0 && errno == EINTR) {
            continue;
        }
        if (length <= 0) {
            if (length < 0 && errno != EAGAIN) {
                const int err = errno;
                logger::error(std::string("Reading inotify events failed: ") + std::strerror(err));
            }
            return;
        }

        for (std::size_t offset = 0; offset < static_cast<std::size_t>(length);) {
            const auto* event = reinterpret_cast<const inotify_event*>(buffer + offset);
            handleEvent(*event);
            offset += sizeof(inotify_event) + event->len;
        }
    }
}

void FileWatcher::handleEvent(const inotify_event& event)
{
    if (event.mask & IN_Q_OVERFLOW) {
        logger::warning("inotify queue overflow: some events were lost");
        logger::event("inotify queue overflow: some events were lost");
        return;
    }

    const std::optional<std::string> renamedFrom = resolvePendingMove(event);

    const auto watch = watches_.find(event.wd);
    if (watch == watches_.end()) {
        return;
    }
    if (event.mask & IN_IGNORED) {
        watches_.erase(watch);
        return;
    }

    const std::string dir = watch->second;
    const std::string name = event.len > 0 ? event.name : "";
    const std::string path = name.empty() ? dir : paths::join(dir, name);

    // A watched subdirectory reports its own events that its parent has already reported.
    const bool duplicate = name.empty() && !isRoot(dir);
    const bool ownScan = consumeOwnScanEvent(event, path);
    if (!duplicate && !ownScan) {
        logEvent(event, path);
    }

    if ((event.mask & IN_ISDIR) && !name.empty()) {
        trackDirectory(event, path, renamedFrom);
    }
    if (name.empty() && isRoot(dir) && (event.mask & kRootGoneEvents)) {
        logger::warning("Watched directory " + dir + " was moved or removed; fix the config and send SIGHUP");
    }
}

// A directory rename arrives as IN_MOVED_FROM immediately followed by IN_MOVED_TO with the same cookie
std::optional<std::string> FileWatcher::resolvePendingMove(const inotify_event& event)
{
    if (!pendingMove_) {
        return std::nullopt;
    }
    PendingMove move = std::move(*pendingMove_);
    pendingMove_.reset();

    if ((event.mask & IN_MOVED_TO) && event.cookie == move.cookie) {
        return move.path;
    }
    unwatchTree(move.path);
    return std::nullopt;
}

// Scanning a new directory makes the kernel report OPEN/ACCESS/CLOSE_NOWRITE to its parent
bool FileWatcher::consumeOwnScanEvent(const inotify_event& event, const std::string& path)
{
    if (!(event.mask & IN_ISDIR) || !(event.mask & kScanEvents)) {
        return false;
    }
    const auto scan = ownScans_.find(path);
    if (scan == ownScans_.end()) {
        return false;
    }
    if (event.mask & IN_CLOSE_NOWRITE) {
        ownScans_.erase(scan);
    }
    return true;
}

void FileWatcher::trackDirectory(const inotify_event& event, const std::string& path,
                                 const std::optional<std::string>& renamedFrom)
{
    if (event.mask & IN_CREATE) {
        watchTree(path, true);
    } else if (event.mask & IN_MOVED_FROM) {
        pendingMove_ = PendingMove{event.cookie, path};
    } else if (event.mask & IN_MOVED_TO) {
        if (renamedFrom) {
            renameTree(*renamedFrom, path);
        } else {
            watchTree(path, true);
        }
    }
}

void FileWatcher::watchTree(const std::string& root, bool parentWatched)
{
    if (parentWatched) {
        ownScans_.insert(root);
    }

    // Collect the tree before adding watches so the scan itself is not reported
    std::vector<std::string> dirs;
    if (!collectSubtree(root, dirs)) {
        const int err = errno;
        ownScans_.erase(root);
        logger::warning("Cannot open directory " + root + ": " + std::strerror(err));
        return;
    }

    for (const auto& dir : dirs) {
        const int wd = inotify_add_watch(fd_.get(), dir.c_str(), kWatchMask);
        if (wd < 0) {
            const int err = errno;
            logger::error("Cannot watch " + dir + ": " + std::strerror(err)
                          + (err == ENOSPC ? " (increase fs.inotify.max_user_watches)" : ""));
            continue;
        }
        watches_[wd] = dir;
    }
}

void FileWatcher::unwatchTree(const std::string& root)
{
    for (auto it = watches_.begin(); it != watches_.end();) {
        if (it->second == root || paths::isInside(it->second, root)) {
            inotify_rm_watch(fd_.get(), it->first);
            it = watches_.erase(it);
        } else {
            ++it;
        }
    }
}

void FileWatcher::renameTree(const std::string& from, const std::string& to)
{
    for (auto& watch : watches_) {
        std::string& dir = watch.second;
        if (dir == from || paths::isInside(dir, from)) {
            dir.replace(0, from.size(), to);
        }
    }
}

bool FileWatcher::isRoot(const std::string& dir) const
{
    return roots_.count(dir) > 0;
}

}
