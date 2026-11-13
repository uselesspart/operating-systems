#include "file_watcher.h"
#include "logger.h"

#include <array>
#include <cerrno>
#include <cstring>
#include <iomanip>
#include <iterator>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

namespace {
    bool is_inside(const std::filesystem::path& path, const std::filesystem::path& parent) {
        const auto relative = path.lexically_relative(parent);
        return !relative.empty() && *relative.begin() != "..";
    }

    std::string quote_path(const std::filesystem::path& path) {
        std::ostringstream text;
        text << '"';
        for (unsigned char c : path.string()) {
            switch (c) {
                case '\\': text << "\\\\"; break;
                case '"': text << "\\\""; break;
                case '\n': text << "\\n"; break;
                case '\r': text << "\\r"; break;
                case '\t': text << "\\t"; break;
                default:
                    if (c < 32 || c == 127) {
                        text << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                             << static_cast<unsigned int>(c) << std::dec;
                    } else {
                        text << c;
                    }
            }
        }
        return text.str() + '"';
    }

    std::string event_names(std::uint32_t mask) {
        const struct {
            std::uint32_t mask;
            const char* name;
        } types[] = {
            {IN_ACCESS, "IN_ACCESS"}, {IN_MODIFY, "IN_MODIFY"},
            {IN_ATTRIB, "IN_ATTRIB"}, {IN_CLOSE_WRITE, "IN_CLOSE_WRITE"},
            {IN_CLOSE_NOWRITE, "IN_CLOSE_NOWRITE"}, {IN_OPEN, "IN_OPEN"},
            {IN_MOVED_FROM, "IN_MOVED_FROM"}, {IN_MOVED_TO, "IN_MOVED_TO"},
            {IN_CREATE, "IN_CREATE"}, {IN_DELETE, "IN_DELETE"},
            {IN_DELETE_SELF, "IN_DELETE_SELF"}, {IN_MOVE_SELF, "IN_MOVE_SELF"},
            {IN_UNMOUNT, "IN_UNMOUNT"}, {IN_Q_OVERFLOW, "IN_Q_OVERFLOW"},
            {IN_IGNORED, "IN_IGNORED"}, {IN_ISDIR, "IN_ISDIR"}
        };
        std::string text;
        for (const auto& type : types) {
            if (mask & type.mask) {
                if (!text.empty()) {
                    text += '|';
                }
                text += type.name;
            }
        }
        return text.empty() ? "UNKNOWN" : text;
    }
}

std::vector<FileEvent> parse_events(const char* buffer, std::size_t size) {
    std::vector<FileEvent> events;
    std::size_t offset = 0;
    while (offset < size) {
        if (size - offset < sizeof(inotify_event)) {
            throw std::runtime_error("Invalid inotify event");
        }
        inotify_event event;
        std::memcpy(&event, buffer + offset, sizeof(event));
        offset += sizeof(event);
        if (event.len > size - offset) {
            throw std::runtime_error("Invalid inotify event");
        }
        std::string name;
        if (event.len != 0) {
            const auto end = static_cast<const char*>(std::memchr(buffer + offset, '\0', event.len));
            if (end == nullptr) {
                throw std::runtime_error("Invalid inotify name");
            }
            name.assign(buffer + offset, end);
        }
        events.push_back({event.wd, event.mask, event.cookie, std::move(name)});
        offset += event.len;
    }
    return events;
}

std::string format_event(const std::filesystem::path& path, const FileEvent& event) {
    const bool directory = (event.mask & IN_ISDIR) != 0 || event.name.empty();
    std::ostringstream text;
    text << "path=" << quote_path(path) << " events=" << event_names(event.mask)
         << " mask=0x" << std::hex << event.mask << std::dec
         << " directory=" << directory << " cookie=" << event.cookie;
    return text.str();
}

FileWatcher::FileWatcher(const std::vector<std::filesystem::path>& paths) : paths(paths) {
    reset(true);
}

FileWatcher::~FileWatcher() {
    if (fd >= 0) {
        close(fd);
    }
}

FileWatcher::Watch FileWatcher::make_watch(const std::filesystem::path& path) {
    struct stat info{};
    if (stat(path.parent_path().c_str(), &info) != 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot watch: " + quote_path(path));
    }
    return Watch{path, info.st_dev, info.st_ino};
}

void FileWatcher::reset(bool strict) {
    const int new_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (new_fd < 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot open inotify");
    }
    std::map<int, Watch> new_watches;
    try {
        for (const auto& path : paths) {
            try {
                auto watch = make_watch(path);
                const int wd = inotify_add_watch(new_fd, path.c_str(), IN_ALL_EVENTS | IN_ONLYDIR);
                if (wd < 0) {
                    throw std::system_error(errno, std::generic_category(), "Cannot watch: " + quote_path(path));
                }
                new_watches.emplace(wd, std::move(watch));
            } catch (const std::system_error& error) {
                if (strict) {
                    throw;
                }
                Logger::error(error.what());
            }
        }
    } catch (...) {
        close(new_fd);
        throw;
    }
    if (fd >= 0) {
        close(fd);
    }
    fd = new_fd;
    watches.swap(new_watches);
}

void FileWatcher::wait(int timeout_ms, bool (*stop)()) {
    pollfd descriptor{fd, POLLIN, 0};
    const int ready = poll(&descriptor, 1, timeout_ms);
    if (ready < 0) {
        if (errno == EINTR) {
            return;
        }
        throw std::system_error(errno, std::generic_category(), "Cannot wait for events");
    }
    if (ready == 0) {
        return;
    }
    if (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) {
        throw std::runtime_error("Inotify unavailable");
    }
    read_events(stop);
}

void FileWatcher::read_events(bool (*stop)()) {
    int queued = 0;
    if (ioctl(fd, FIONREAD, &queued) != 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot inspect events");
    }
    auto remaining = static_cast<std::size_t>(queued);
    std::array<char, 65536> buffer;
    std::vector<FileEvent> events;
    while (remaining > 0 && (stop == nullptr || !stop())) {
        ssize_t size;
        do {
            size = read(fd, buffer.data(), buffer.size());
        } while (size < 0 && errno == EINTR);
        if (size < 0) {
            if (errno == EAGAIN) {
                break;
            }
            throw std::system_error(errno, std::generic_category(), "Cannot read events");
        }
        if (size == 0) {
            throw std::runtime_error("Inotify closed");
        }
        auto batch = parse_events(buffer.data(), static_cast<std::size_t>(size));
        events.insert(events.end(), std::make_move_iterator(batch.begin()), std::make_move_iterator(batch.end()));
        const auto count = static_cast<std::size_t>(size);
        remaining = count >= remaining ? 0 : remaining - count;
    }
    process_events(events, stop);
}

void FileWatcher::drain(bool (*stop)()) {
    read_events(stop);
}

bool FileWatcher::update(const std::vector<std::filesystem::path>& next_paths, bool (*stop)()) {
    drain(stop);
    if (stop != nullptr && stop()) {
        return false;
    }
    auto saved_paths = next_paths;
    auto next_watches = watches;
    std::set<int> retained;
    std::vector<int> added;
    added.reserve(next_paths.size());
    auto rollback = [&]() {
        for (int wd : added) {
            const auto it = next_watches.find(wd);
            if (it != next_watches.end()) {
                it->second.active = false;
            }
            if (inotify_rm_watch(fd, wd) < 0 && errno != EINVAL) {
                Logger::error(std::system_error(errno, std::generic_category(), "Cannot remove watch").what());
            }
        }
        watches.merge(next_watches);
    };
    try {
        for (const auto& path : next_paths) {
            auto watch = make_watch(path);
            const int wd = inotify_add_watch(fd, path.c_str(), IN_ALL_EVENTS | IN_ONLYDIR);
            if (wd < 0) {
                throw std::system_error(errno, std::generic_category(), "Cannot watch: " + quote_path(path));
            }
            if (watches.find(wd) == watches.end()) {
                added.push_back(wd);
            }
            next_watches.insert_or_assign(wd, std::move(watch));
            retained.insert(wd);
        }
    } catch (...) {
        rollback();
        throw;
    }
    if (stop != nullptr && stop()) {
        rollback();
        return false;
    }
    for (const auto& [wd, watch] : watches) {
        if (watch.active && retained.find(wd) == retained.end()) {
            if (inotify_rm_watch(fd, wd) < 0 && errno != EINVAL) {
                const int code = errno;
                rollback();
                throw std::system_error(code, std::generic_category(), "Cannot remove watch");
            }
        }
    }
    watches.swap(next_watches);
    paths.swap(saved_paths);
    return true;
}

void FileWatcher::swap(FileWatcher& other) noexcept {
    std::swap(fd, other.fd);
    paths.swap(other.paths);
    watches.swap(other.watches);
}

void FileWatcher::process_events(const std::vector<FileEvent>& events, bool (*stop)()) {
    std::set<int> deleted;
    for (const auto& event : events) {
        if (event.mask & IN_DELETE_SELF) {
            deleted.insert(event.wd);
        }
    }
    for (const auto& event : events) {
        if (stop != nullptr && stop()) {
            return;
        }
        if (event.mask & IN_Q_OVERFLOW) {
            Logger::error("Inotify overflow: events lost");
            reset(false);
            Logger::info("Watches restored: " + std::to_string(watches.size()));
            return;
        }
        const auto it = watches.find(event.wd);
        if (it == watches.end()) {
            Logger::error("Unknown watch: " + std::to_string(event.wd));
            continue;
        }
        auto& watch = it->second;
        const bool lost = (event.mask & (IN_DELETE_SELF | IN_MOVE_SELF | IN_UNMOUNT | IN_IGNORED)) != 0;
        if (!watch.active && !lost) {
            continue;
        }
        if (!lost && deleted.find(event.wd) == deleted.end()) {
            struct stat info{};
            if (stat(watch.path.parent_path().c_str(), &info) != 0 ||
                info.st_dev != watch.parent_device || info.st_ino != watch.parent_inode) {
                remove_directory(watch.path);
                continue;
            }
        }
        const auto path = event.name.empty() ? watch.path : watch.path / event.name;
        Logger::event(format_event(path, event));
        if ((event.mask & IN_ISDIR) && (event.mask & (IN_MOVED_FROM | IN_DELETE))) {
            remove_directory(path);
        }
        if (event.mask & IN_MOVE_SELF) {
            remove_directory(watch.path);
        } else if (lost && watch.active) {
            Logger::error("Watch lost: " + quote_path(watch.path));
            watch.active = false;
        }
        if (event.mask & IN_IGNORED) {
            watches.erase(it);
        }
    }
}

void FileWatcher::remove_watch(int wd, Watch& watch) {
    Logger::error("Watch lost: " + quote_path(watch.path));
    watch.active = false;
    if (inotify_rm_watch(fd, wd) < 0 && errno != EINVAL) {
        throw std::system_error(errno, std::generic_category(), "Cannot remove watch");
    }
}

void FileWatcher::remove_directory(const std::filesystem::path& path) {
    for (auto& [wd, watch] : watches) {
        if (!watch.active) {
            continue;
        }
        if (is_inside(watch.path, path)) {
            remove_watch(wd, watch);
        }
    }
}
