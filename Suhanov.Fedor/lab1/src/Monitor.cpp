#include "Monitor.hpp"

#include <array>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include <sys/inotify.h>
#include <syslog.h>
#include <unistd.h>

namespace application {

static constexpr std::uint32_t EVENT_MASK =
    IN_ACCESS | IN_ATTRIB | IN_CLOSE_WRITE | IN_CLOSE_NOWRITE |
    IN_CREATE | IN_DELETE | IN_DELETE_SELF | IN_MODIFY |
    IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO | IN_OPEN;

static const char* event_name(std::uint32_t mask) {
    if (mask & IN_ACCESS) return "ACCESS";
    if (mask & IN_ATTRIB) return "ATTRIB";
    if (mask & IN_CLOSE_WRITE) return "CLOSE_WRITE";
    if (mask & IN_CLOSE_NOWRITE) return "CLOSE_NOWRITE";
    if (mask & IN_CREATE) return "CREATE";
    if (mask & IN_DELETE) return "DELETE";
    if (mask & IN_DELETE_SELF) return "DELETE_SELF";
    if (mask & IN_MODIFY) return "MODIFY";
    if (mask & IN_MOVE_SELF) return "MOVE_SELF";
    if (mask & IN_MOVED_FROM) return "MOVED_FROM";
    if (mask & IN_MOVED_TO) return "MOVED_TO";
    if (mask & IN_OPEN) return "OPEN";
    if (mask & IN_IGNORED) return "IGNORED";
    return "UNKNOWN";
}

Monitor::Monitor(AppConfig config)
    : config_{std::move(config)} {
    fd_ = ::inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd_ == -1) {
        throw std::runtime_error(std::string("inotify_init1 failed: ") + std::strerror(errno));
    }

    try {
        wds_.reserve(config_.targets.size());
        for (const auto& file : config_.targets) {
            int wd = ::inotify_add_watch(fd_, file.c_str(), EVENT_MASK);
            if (wd == -1) {
                throw std::runtime_error("cannot watch " + file.string() + ": " + std::strerror(errno));
            }
            wds_.push_back(wd);
        }
    } catch (...) {
        clear();
        ::close(fd_);
        fd_ = -1;
        throw;
    }
}

Monitor::~Monitor() {
    if (fd_ != -1) {
        clear();
        ::close(fd_);
    }
}

void Monitor::clear() {
    if (fd_ == -1) {
        return;
    }
    for (int wd : wds_) {
        ::inotify_rm_watch(fd_, wd);
    }
    wds_.clear();
}

void Monitor::process_event(const inotify_event* event) {
    if (event->mask & IN_Q_OVERFLOW) {
        ::syslog(LOG_WARNING, "inotify event queue overflow");
        return;
    }

    for (std::size_t i = 0; i < wds_.size(); ++i) {
        if (wds_[i] != event->wd) {
            continue;
        }

        Path path = config_.targets[i];
        if (event->len > 0 && event->name[0] != '\0') {
            path /= event->name;
        }
        const std::string name = path.string();
        ::syslog(LOG_INFO, "event=%s path=%s mask=0x%x",
                 event_name(event->mask), name.c_str(), event->mask);
        return;
    }
}

bool Monitor::read_events() {
    alignas(inotify_event) std::array<char, 4096> buff{};

    for (;;) {
        const ssize_t size = ::read(fd_, buff.data(), buff.size());
        if (size == -1) {
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN) {
                return true;
            }
            ::syslog(LOG_ERR, "inotify read failed: %m");
            return false;
        }
        if (size == 0) {
            ::syslog(LOG_ERR, "inotify read returned no data");
            return false;
        }

        const std::size_t count = static_cast<std::size_t>(size);
        for (std::size_t offset = 0; offset < count;) {
            if (count - offset < sizeof(inotify_event)) {
                ::syslog(LOG_ERR, "incomplete inotify event");
                return false;
            }
            const auto* event = reinterpret_cast<const inotify_event*>(buff.data() + offset);
            const std::size_t event_size = sizeof(inotify_event) + event->len;
            if (event_size > count - offset) {
                ::syslog(LOG_ERR, "incomplete inotify event");
                return false;
            }
            process_event(event);
            offset += event_size;
        }
    }
}

} // namespace application

