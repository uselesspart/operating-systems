#include "link/FdOps.hpp"

#include "util/Errors.hpp"

#include <cerrno>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace chatlab::fdops {

namespace {

template <typename Step>
bool repeatUntilDone(std::size_t size, Step step) {
    std::size_t done = 0;
    while (done < size) {
        const ssize_t moved = step(done, size - done);
        if (moved < 0 && errno == EINTR) {
            continue;
        }
        if (moved <= 0) {
            return false;
        }
        done += static_cast<std::size_t>(moved);
    }
    return true;
}

}

bool readFull(int fd, void* data, std::size_t size) {
    auto* bytes = static_cast<char*>(data);
    return repeatUntilDone(size, [&](std::size_t from, std::size_t left) {
        return ::read(fd, bytes + from, left);
    });
}

bool writeFull(int fd, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    return repeatUntilDone(size, [&](std::size_t from, std::size_t left) {
        return ::write(fd, bytes + from, left);
    });
}

bool sendFull(int fd, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const char*>(data);
    return repeatUntilDone(size, [&](std::size_t from, std::size_t left) {
        return ::send(fd, bytes + from, left, MSG_NOSIGNAL);
    });
}

bool waitReadable(int fd, std::chrono::milliseconds timeout) {
    pollfd watched{fd, POLLIN, 0};
    int ready = 0;
    do {
        ready = ::poll(&watched, 1, static_cast<int>(timeout.count()));
    } while (ready < 0 && errno == EINTR);
    return ready > 0;
}

void setBlocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags & ~O_NONBLOCK) < 0) {
        raiseErrno("fcntl");
    }
}

}
