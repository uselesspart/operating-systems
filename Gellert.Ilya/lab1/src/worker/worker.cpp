#include "worker/worker.hpp"

#include <sys/inotify.h>
#include <sys/select.h>
#include <unistd.h>
#include <syslog.h>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace monitor {

Worker::Worker() : inotifyFd_(-1) {}

Worker::~Worker() {
    cleanup();
}

void Worker::cleanup() {
    if (inotifyFd_ >= 0) {
        for (const auto& [wd, dir] : watchMap_) {
            inotify_rm_watch(inotifyFd_, wd);
        }
        close(inotifyFd_);
        inotifyFd_ = -1;
    }
    watchMap_.clear();
}

void Worker::init(const std::vector<std::string>& dirs) {
    cleanup();

    inotifyFd_ = inotify_init1(IN_NONBLOCK);
    if (inotifyFd_ < 0) {
        throw std::runtime_error("inotify_init1 error: " + std::string(strerror(errno)));
    }

    uint32_t watchMask = IN_ACCESS | IN_MODIFY | IN_CREATE | IN_DELETE;

    for (const auto& dir : dirs) {
        int wd = inotify_add_watch(inotifyFd_, dir.c_str(), watchMask);
        if (wd < 0) {
            syslog(LOG_LOCAL0 | LOG_ERR, "Failed to add the directory to inotify: %s (%s)", 
                   dir.c_str(), strerror(errno));
        } else {
            watchMap_[wd] = dir;
            syslog(LOG_LOCAL0 | LOG_INFO, "Successfully added monitoring for directory: %s", dir.c_str());
        }
    }

    if (watchMap_.empty()) {
        syslog(LOG_LOCAL0 | LOG_WARNING, "No successfully added directories for monitoring.");
    }
}

void Worker::pollEvents() {
    if (inotifyFd_ < 0) return;

    fd_set readFds;
    FD_ZERO(&readFds);
    FD_SET(inotifyFd_, &readFds);

    int ret = select(inotifyFd_ + 1, &readFds, nullptr, nullptr, nullptr);

    if (ret < 0) {
        if (errno != EINTR) {
            syslog(LOG_ERR, "Select error: %s", strerror(errno));
        }
        return;
    }

    if (ret > 0 && FD_ISSET(inotifyFd_, &readFds)) {
        const size_t bufSize = 4096;
        char buffer[bufSize];

        while (true) {
            ssize_t length = read(inotifyFd_, buffer, sizeof(buffer));
            if (length < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    break;
                } else {
                    syslog(LOG_ERR, "Read error: %s", strerror(errno));
                    break;
                }
            }

            for (char* ptr = buffer; ptr < buffer + length; ) {
                struct inotify_event* event = reinterpret_cast<struct inotify_event*>(ptr);

                if (event->len > 0) {
                    std::string dirName = watchMap_[event->wd];
                    std::string action;

                    if (event->mask & IN_CREATE) action = "CREATED";
                    else if (event->mask & IN_DELETE) action = "DELETED";
                    else if (event->mask & IN_MODIFY) action = "MODIFIED";
                    else if (event->mask & IN_ACCESS) action = "ACCESSED";

                    if (!action.empty()) {
                        syslog(LOG_INFO, "[%s] %s: %s/%s", 
                               action.c_str(), 
                               (event->mask & IN_ISDIR) ? "Directory" : "File",
                               dirName.c_str(), 
                               event->name);
                    }
                }
                ptr += sizeof(struct inotify_event) + event->len;
            }
        }
    }
}

} // namespace monitor