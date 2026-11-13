#include "InotifyWatcher.hpp"

#include "Journal.hpp"
#include "PathUtil.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

namespace diskmond {

namespace {


constexpr std::uint32_t kInotifyMask = IN_ALL_EVENTS | IN_DONT_FOLLOW | IN_ONLYDIR;

constexpr std::uint32_t kListingEvents = IN_OPEN | IN_ACCESS | IN_CLOSE_NOWRITE;
constexpr std::size_t kReadChunk = 64 * 1024;
constexpr std::size_t kEventsPerTurn = 4096;
constexpr int kMaxPullReads = 64;

constexpr int kMovePairWaitMs = 10;

struct DirectoryCloser {
    void operator()(DIR* directory) const noexcept { ::closedir(directory); }
};
using DirectoryHandle = std::unique_ptr<DIR, DirectoryCloser>;


bool isSubdirectory(DIR* directory, const dirent& entry) {
    if (entry.d_type != DT_UNKNOWN) {
        return entry.d_type == DT_DIR;
    }
    struct stat info {};
    return ::fstatat(::dirfd(directory), entry.d_name, &info, AT_SYMLINK_NOFOLLOW) == 0 &&
           S_ISDIR(info.st_mode);
}

std::string directories(std::size_t count) {
    return std::to_string(count) + (count == 1 ? " directory" : " directories");
}

}  

bool InotifyWatcher::open(std::string& error) {
    close();
    fd_.reset(::inotify_init1(IN_NONBLOCK | IN_CLOEXEC));
    if (!fd_.isOpen()) {
        const int code = errno;
        error = std::string("inotify_init1 failed: ") + std::strerror(code);
        return false;
    }
    return true;
}

void InotifyWatcher::close() {
    if (!fd_.isOpen()) {
        return;
    }
    concludeMoveOut();
    fd_.reset();  
    tree_.clear();
    queue_.clear();
    ownListings_.clear();
    expectedCreates_.clear();
}

void InotifyWatcher::apply(const Settings& settings) {
    concludeMoveOut();
    const bool ignoreListChanged = settings.ignored != settings_.ignored;
    settings_ = settings;
    watchLimitReported_ = false;

    if (ignoreListChanged) {
        
        detachAll();
    } else {
        const auto& wanted = settings_.directories;
        for (const int root : tree_.roots()) {
            const auto where = tree_.pathOf(root);
            if (where && std::find(wanted.begin(), wanted.end(), *where) == wanted.end()) {
                detachTree(root);
                journal::status(LOG_INFO, "no longer watching " + *where);
            }
        }
    }

    for (const auto& directory : settings_.directories) {
        if (tree_.findRoot(directory)) {
            continue;
        }
        if (isFiltered(directory)) {
            journal::status(LOG_WARNING,
                            directory + " is listed in both 'watch' and 'ignore'; not watching it");
            continue;
        }
        attachTree(directory, -1, {}, Scan::Configured);
        if (const auto root = tree_.findRoot(directory)) {
            journal::status(LOG_INFO,
                            "watching " + directory + " (" + directories(tree_.subtreeSize(*root)) + ")");
        }
    }
}

void InotifyWatcher::consumeEvents() {
    for (std::size_t handled = 0; handled < kEventsPerTurn && fd_.isOpen();) {
        if (queue_.empty()) {
            const ReadStatus status = readKernelQueue();
            if (status == ReadStatus::Read) {
                continue;
            }
            if (status == ReadStatus::Empty && pairMayStillArrive()) {
                continue;
            }
            concludeMoveOut();  
            return;
        }
        const Event event = std::move(queue_.front());
        queue_.pop_front();
        dispatch(event);
        ++handled;
    }
    
}

InotifyWatcher::ReadStatus InotifyWatcher::readKernelQueue() {
    char chunk[kReadChunk];
    ssize_t length = -1;
    do {
        length = ::read(fd_.get(), chunk, sizeof chunk);
    } while (length < 0 && errno == EINTR);

    if (length <= 0) {
        const int code = errno;
        if (length < 0 && code != EAGAIN && code != EWOULDBLOCK) {
            journal::status(LOG_ERR, std::string("cannot read from inotify: ") + std::strerror(code));
            return ReadStatus::Failed;
        }
        return ReadStatus::Empty;
    }

    
    std::size_t offset = 0;
    while (offset + sizeof(inotify_event) <= static_cast<std::size_t>(length)) {
        inotify_event header{};
        std::memcpy(&header, chunk + offset, sizeof header);
        const char* name = chunk + offset + sizeof header;
        queue_.push_back(Event{header.wd, header.mask, header.cookie,
                               std::string(name, ::strnlen(name, header.len)), nextSeq_++});
        offset += sizeof header + header.len;
    }
    return ReadStatus::Read;
}

void InotifyWatcher::pullAvailableEvents() {
    for (int reads = 0; reads < kMaxPullReads; ++reads) {
        if (readKernelQueue() != ReadStatus::Read) {
            return;
        }
    }
}

bool InotifyWatcher::pairMayStillArrive() const {
    if (!moveSource_) {
        return false;
    }
    pollfd watched{fd_.get(), POLLIN, 0};
    return ::poll(&watched, 1, kMovePairWaitMs) > 0;
}

void InotifyWatcher::dispatch(const Event& event) {
    if (event.mask & IN_Q_OVERFLOW) {
        concludeMoveOut();
        journal::status(LOG_WARNING, "inotify queue overflowed, some events were lost; rescanning all trees");
        journal::fileEvent("<overflow> some events were lost");
        rescan();
        return;
    }

    if (event.mask & IN_IGNORED) {
        
        if (tree_.contains(event.wd)) {
            const bool root = tree_.isRoot(event.wd);
            const auto where = tree_.pathOf(event.wd);
            detachTree(event.wd);
            if (root && where) {
                journal::status(LOG_WARNING, "watched root " + *where +
                                                 " is gone; recreate it and send SIGHUP to watch it again");
            }
        }
        return;
    }

    const bool completesMove =
        moveSource_ && (event.mask & IN_MOVED_TO) && event.cookie == moveSource_->cookie;
    if (moveSource_ && !completesMove) {
        concludeMoveOut();  
    }

    const auto directory = tree_.pathOf(event.wd);
    if (!directory) {
        return;  
    }

    const bool root = tree_.isRoot(event.wd);
    
    if (event.name.empty() && !root && (event.mask & IN_UNMOUNT) == 0) {
        return;
    }
    if (consumeOwnListing(event)) {
        return;
    }

    const bool isDirectory = (event.mask & IN_ISDIR) || event.name.empty();
    const std::string path = event.name.empty() ? *directory : path::join(*directory, event.name);

    if (event.mask & IN_MOVED_FROM) {
        moveSource_ = MoveSource{event.cookie, isDirectory, path,
                                 isDirectory ? tree_.childOf(event.wd, event.name) : std::nullopt};
        return;
    }
    if (event.mask & IN_MOVED_TO) {
        std::optional<MoveSource> from;
        if (completesMove) {
            from.swap(moveSource_);
        }
        onMovedTo(event, path, isDirectory, std::move(from));
        return;
    }

    const auto action = actionForMask(event.mask);
    if (!action) {
        return;
    }
    const bool alreadyWritten = (event.mask & IN_CREATE) && consumeExpectedCreate(event.wd, event.name);
    if (!alreadyWritten && !isFiltered(path)) {
        writeEntry(*action, isDirectory, path);
    }

    if ((event.mask & IN_CREATE) && isDirectory) {
        attachTree(path, event.wd, event.name, Scan::Created);
    } else if (root && event.name.empty() && (event.mask & IN_MOVE_SELF)) {
        
        journal::status(LOG_WARNING,
                        "watched root " + path +
                            " was moved away and is not watched any more; update the config and send SIGHUP");
        detachTree(event.wd);
    }
}

void InotifyWatcher::onMovedTo(const Event& event, const std::string& path, bool isDirectory,
                               std::optional<MoveSource> from) {
    const bool targetFiltered = isFiltered(path);
    if (!from) {
        
        if (!targetFiltered) {
            writeEntry(Action::MoveIn, isDirectory, path);
            if (isDirectory) {
                attachTree(path, event.wd, event.name, Scan::Arrived);
            }
        }
        return;
    }

    const bool sourceFiltered = isFiltered(from->path);
    if (!sourceFiltered && !targetFiltered) {
        const bool sameDirectory = path::parentOf(from->path) == path::parentOf(path);
        writeEntry(sameDirectory ? Action::Rename : Action::Move, isDirectory, from->path, path);
    } else if (!sourceFiltered) {
        writeEntry(Action::MoveOut, isDirectory, from->path);
    } else if (!targetFiltered) {
        writeEntry(Action::MoveIn, isDirectory, path);
    }

    if (!isDirectory) {
        return;
    }
    
    if (!targetFiltered && from->wd && tree_.move(*from->wd, event.wd, event.name)) {
        return;
    }
    if (from->wd) {
        detachTree(*from->wd);
    }
    if (!targetFiltered) {
        attachTree(path, event.wd, event.name, Scan::Arrived);
    }
}

void InotifyWatcher::concludeMoveOut() {
    if (!moveSource_) {
        return;
    }
    std::optional<MoveSource> source;
    source.swap(moveSource_);
    if (!isFiltered(source->path)) {
        writeEntry(Action::MoveOut, source->isDirectory, source->path);
    }
    if (source->wd) {
        detachTree(*source->wd);  
    }
}

bool InotifyWatcher::consumeOwnListing(const Event& event) {
    if (ownListings_.empty() || (event.mask & kListingEvents) == 0) {
        return false;
    }
    if (!event.name.empty() && (event.mask & IN_ISDIR) == 0) {
        return false;
    }
    const auto listing = ownListings_.find({event.wd, event.name});
    if (listing == ownListings_.end() || event.seq < listing->second.since) {
        return false;  
    }
    if ((event.mask & IN_CLOSE_NOWRITE) && --listing->second.pending <= 0) {
        
        expectedCreates_.erase(listing->second.listedWd);
        ownListings_.erase(listing);
    }
    return true;
}

bool InotifyWatcher::consumeExpectedCreate(int wd, const std::string& name) {
    const auto expected = expectedCreates_.find(wd);
    return expected != expectedCreates_.end() && expected->second.erase(name) > 0;
}

void InotifyWatcher::attachTree(const std::string& path, int parentWd, const std::string& name, Scan reason) {
    struct Item {
        std::string path;
        int parentWd;  
        std::string name;
    };

    std::vector<Item> pending{{path, parentWd, name}};
    while (!pending.empty()) {
        const Item item = std::move(pending.back());
        pending.pop_back();
        const bool isRoot = item.parentWd < 0;
        if (isFiltered(item.path)) {
            continue;
        }

        const int wd = ::inotify_add_watch(fd_.get(), item.path.c_str(), kInotifyMask);
        if (wd < 0) {
            const int code = errno;
            reportWatchFailure(item.path, code, isRoot);
            continue;
        }
        if (tree_.contains(wd)) {
            continue;  
        }
        const bool added =
            isRoot ? tree_.addRoot(wd, item.path) : tree_.addChild(wd, item.parentWd, item.name);
        if (!added) {
            ::inotify_rm_watch(fd_.get(), wd);
            continue;
        }

        
        pullAvailableEvents();
        const ListingKey key = isRoot ? ListingKey{wd, std::string()} : ListingKey{item.parentWd, item.name};
        OwnListing& listing = ownListings_[key];
        if (listing.pending++ == 0) {
            listing.since = nextSeq_;
            listing.listedWd = wd;
        }

        const DirectoryHandle directory(::opendir(item.path.c_str()));
        if (!directory) {
            forgetListing(key);
            continue;
        }
        while (const dirent* entry = ::readdir(directory.get())) {
            const std::string child = entry->d_name;
            if (child == "." || child == "..") {
                continue;
            }
            const bool childIsDirectory = isSubdirectory(directory.get(), *entry);
            const std::string childPath = path::join(item.path, child);
            if (reason == Scan::Created && !isFiltered(childPath)) {
                
                writeEntry(Action::Create, childIsDirectory, childPath);
                expectedCreates_[wd].insert(child);
            }
            if (childIsDirectory) {
                pending.push_back({childPath, wd, child});
            }
        }
    }
}

void InotifyWatcher::forgetListing(const ListingKey& key) {
    const auto listing = ownListings_.find(key);
    if (listing != ownListings_.end() && --listing->second.pending <= 0) {
        expectedCreates_.erase(listing->second.listedWd);
        ownListings_.erase(listing);
    }
}

void InotifyWatcher::detachTree(int wd) {
    for (const int removed : tree_.removeSubtree(wd)) {
        ::inotify_rm_watch(fd_.get(), removed);
        expectedCreates_.erase(removed);
        auto listing = ownListings_.lower_bound({removed, std::string()});
        while (listing != ownListings_.end() && listing->first.first == removed) {
            listing = ownListings_.erase(listing);
        }
    }
}

void InotifyWatcher::detachAll() {
    for (const int wd : tree_.ids()) {
        ::inotify_rm_watch(fd_.get(), wd);
    }
    tree_.clear();
    ownListings_.clear();
    expectedCreates_.clear();
}

void InotifyWatcher::rescan() {
    detachAll();
    for (const auto& directory : settings_.directories) {
        attachTree(directory, -1, {}, Scan::Arrived);
    }
}

void InotifyWatcher::reportWatchFailure(const std::string& path, int error, bool isRoot) {
    if (error == ENOSPC) {
        if (!watchLimitReported_) {
            journal::status(LOG_ERR, "cannot watch " + path +
                                         ": out of inotify watches, part of the tree is not watched"
                                         " (raise fs.inotify.max_user_watches)");
            watchLimitReported_ = true;
        }
        return;
    }
    
    if (!isRoot && (error == ENOENT || error == ENOTDIR)) {
        return;
    }
    journal::status(LOG_WARNING, "cannot watch " + path + ": " + std::strerror(error));
}

bool InotifyWatcher::isFiltered(const std::string& path) const {
    if (path == settings_.logFile) {
        return true;
    }
    return std::any_of(settings_.ignored.begin(), settings_.ignored.end(),
                       [&path](const std::string& ignored) { return path::isWithin(path, ignored); });
}

void InotifyWatcher::writeEntry(Action action, bool isDirectory, const std::string& path,
                                const std::string& target) {
    journal::fileEvent(formatEntry(action, isDirectory, path, target));
}

}  
