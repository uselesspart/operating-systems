#include "ipc/FdIo.h"

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>

namespace wolf::fdio {

namespace {

using Clock = std::chrono::steady_clock;

int remainingMs(Clock::time_point deadline) {
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    return left.count() > 0 ? static_cast<int>(left.count()) : 0;
}

bool waitFor(int fd, short events, Clock::time_point deadline) {
    for (;;) {
        pollfd pfd{fd, events, 0};
        const int ready = ::poll(&pfd, 1, remainingMs(deadline));
        if (ready > 0) return true;
        if (ready == 0) return false;
        if (errno != EINTR) return false;
    }
}

}

bool waitReadable(int fd, std::chrono::milliseconds timeout) {
    return waitFor(fd, POLLIN, Clock::now() + timeout);
}

bool readExact(int fd, void* buf, std::size_t count, std::chrono::milliseconds timeout) {
    auto* p = static_cast<char*>(buf);
    const auto deadline = Clock::now() + timeout;
    while (count > 0) {
        const ssize_t n = ::read(fd, p, count);
        if (n > 0) {
            p += n;
            count -= static_cast<std::size_t>(n);
            continue;
        }
        if (n == 0) return false;
        if (errno == EINTR) continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return false;
        if (!waitFor(fd, POLLIN, deadline)) return false;
    }
    return true;
}

bool writeExact(int fd, const void* buf, std::size_t count, std::chrono::milliseconds timeout) {
    const auto* p = static_cast<const char*>(buf);
    const auto deadline = Clock::now() + timeout;
    while (count > 0) {
        const ssize_t n = ::write(fd, p, count);
        if (n > 0) {
            p += n;
            count -= static_cast<std::size_t>(n);
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) return false;
        if (!waitFor(fd, POLLOUT, deadline)) return false;
    }
    return true;
}

bool setNonBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL);
    return flags >= 0 && ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

void closeAllExcept(const std::vector<int>& keep) {
    DIR* dir = ::opendir("/proc/self/fd");
    if (dir == nullptr) return;
    const int dirFd = ::dirfd(dir);
    std::vector<int> fds;
    while (const dirent* entry = ::readdir(dir)) {
        if (entry->d_name[0] == '.') continue;
        char* end = nullptr;
        const long value = std::strtol(entry->d_name, &end, 10);
        if (end == entry->d_name || *end != '\0') continue;
        fds.push_back(static_cast<int>(value));
    }
    ::closedir(dir);

    for (int fd : fds) {
        if (fd <= STDERR_FILENO || fd == dirFd) continue;
        bool needed = false;
        for (int kept : keep) {
            if (fd == kept) {
                needed = true;
                break;
            }
        }
        if (!needed) ::close(fd);
    }
}

}
