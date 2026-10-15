#include "FileWatcher.h"

#include "EventFormatter.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/inotify.h>
#include <unistd.h>

#include <filesystem>
#include <sstream>
#include <stdexcept>

namespace disk_monitor {
namespace {

constexpr std::uint32_t watch_mask = IN_ALL_EVENTS;

std::string keyFor(const std::filesystem::path& path) {
    return path.lexically_normal().generic_string();
}

bool isSameOrBelow(const std::filesystem::path& candidate,
                  const std::filesystem::path& directory) {
    const auto relative = candidate.lexically_relative(directory);
    return relative.empty() || relative == "." ||
           (relative.begin() != relative.end() &&
            *relative.begin() != std::filesystem::path(".."));
}

}  // namespace

FileWatcher::FileWatcher(Logger& logger) : logger_(logger), inotify_fd_(inotify_init1(IN_NONBLOCK | IN_CLOEXEC)) {
    if (!inotify_fd_) {
        throw std::runtime_error(std::string("cannot initialize inotify: ") + std::strerror(errno));
    }
}

int FileWatcher::descriptor() const noexcept {
    return inotify_fd_.get();
}

void FileWatcher::removeAllWatches() {
    for (const auto& [watch_descriptor, watch] : watches_) {
        (void)watch;
        inotify_rm_watch(inotify_fd_.get(), watch_descriptor);
    }
    watches_.clear();
    directory_to_watch_.clear();
}

bool FileWatcher::isWatched(const std::filesystem::path& directory) const {
    return directory_to_watch_.find(keyFor(directory)) != directory_to_watch_.end();
}

bool FileWatcher::isCoveredByExistingWatch(const std::filesystem::path& directory) const {
    for (const auto& [watch_descriptor, watch] : watches_) {
        (void)watch_descriptor;
        if (isSameOrBelow(directory, watch.directory)) {
            return true;
        }
    }
    return false;
}

void FileWatcher::addDirectory(const std::filesystem::path& directory) {
    const auto normalized = directory.lexically_normal();
    if (isWatched(normalized)) {
        return;
    }

    std::error_code error;
    if (!std::filesystem::is_directory(normalized, error) || error) {
        logger_.warning("cannot watch directory '" + normalized.string() + "': " +
                        (error ? error.message() : "directory does not exist"));
        return;
    }

    const int watch_descriptor = inotify_add_watch(inotify_fd_.get(), normalized.c_str(), watch_mask);
    if (watch_descriptor < 0) {
        logger_.warning("cannot watch directory '" + normalized.string() + "': " +
                        std::strerror(errno));
        return;
    }
    watches_.emplace(watch_descriptor, Watch{normalized});
    directory_to_watch_.emplace(keyFor(normalized), watch_descriptor);
}

void FileWatcher::addDirectoryTree(const std::filesystem::path& directory) {
    addDirectory(directory);

    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(
        directory, std::filesystem::directory_options::skip_permission_denied, error);
    const std::filesystem::recursive_directory_iterator end;
    while (iterator != end) {
        if (error) {
            logger_.warning("cannot enumerate directory '" + directory.string() + "': " +
                            error.message());
            error.clear();
            iterator.increment(error);
            continue;
        }
        std::error_code entry_error;
        if (iterator->is_directory(entry_error) && !iterator->is_symlink(entry_error)) {
            addDirectory(iterator->path());
        }
        iterator.increment(error);
    }
}

void FileWatcher::removeDirectoryTree(const std::filesystem::path& directory) {
    for (auto iterator = watches_.begin(); iterator != watches_.end();) {
        if (isSameOrBelow(iterator->second.directory, directory)) {
            directory_to_watch_.erase(keyFor(iterator->second.directory));
            inotify_rm_watch(inotify_fd_.get(), iterator->first);
            iterator = watches_.erase(iterator);
        } else {
            ++iterator;
        }
    }
}

void FileWatcher::reload(const std::vector<std::filesystem::path>& roots) {
    removeAllWatches();
    for (const auto& root : roots) {
        if (isCoveredByExistingWatch(root)) {
            continue;
        }
        addDirectoryTree(root);
    }
}

void FileWatcher::logEvent(const struct inotify_event& event,
                           const std::filesystem::path& path) const {
    std::ostringstream message;
    message << "event=" << eventMaskToText(event.mask) << " type=" << eventKindToText(event.mask)
            << " path=" << path.string();
    if (event.cookie != 0) {
        message << " cookie=" << event.cookie;
    }
    logger_.event(message.str());
}

void FileWatcher::consumeEvents() {
    alignas(inotify_event) char buffer[64 * 1024];
    while (true) {
        const ssize_t bytes_read = read(inotify_fd_.get(), buffer, sizeof(buffer));
        if (bytes_read < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                return;
            }
            logger_.error(std::string("inotify read failed: ") + std::strerror(errno));
            return;
        }
        if (bytes_read == 0) {
            return;
        }

        ssize_t offset = 0;
        while (offset < bytes_read) {
            const auto* event = reinterpret_cast<const inotify_event*>(buffer + offset);
            const auto watch = watches_.find(event->wd);
            if ((event->mask & IN_Q_OVERFLOW) != 0) {
                logger_.error("inotify event queue overflowed; some events may be lost");
            } else if (watch != watches_.end()) {
                std::filesystem::path path = watch->second.directory;
                if (event->len != 0) {
                    path /= std::string(event->name);
                }
                if ((event->mask & IN_IGNORED) == 0) {
                    logEvent(*event, path);
                }
                if ((event->mask & (IN_CREATE | IN_MOVED_TO)) != 0 &&
                    (event->mask & IN_ISDIR) != 0) {
                    addDirectoryTree(path);
                }
                if ((event->mask & (IN_DELETE | IN_MOVED_FROM)) != 0 &&
                    (event->mask & IN_ISDIR) != 0) {
                    removeDirectoryTree(path);
                }
                if ((event->mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_IGNORED)) != 0) {
                    const auto watched_directory = watch->second.directory;
                    removeDirectoryTree(watched_directory);
                }
            }
            offset += static_cast<ssize_t>(sizeof(inotify_event) + event->len);
        }
    }
}

}  // namespace disk_monitor

