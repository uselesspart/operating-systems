#include "ipc/FdIo.h"

#include "util/SystemError.h"

#include <cerrno>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace chat::fdio {

namespace {

template <typename Transfer>
bool transferAll(std::size_t count, Transfer transfer)
{
    std::size_t done = 0;
    while (done < count) {
        const ssize_t n = transfer(done, count - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    return true;
}

}

bool readExact(int fd, void* buffer, std::size_t count)
{
    auto* bytes = static_cast<char*>(buffer);
    return transferAll(
        count, [&](std::size_t offset, std::size_t left) { return ::read(fd, bytes + offset, left); });
}

bool writeExact(int fd, const void* buffer, std::size_t count)
{
    const auto* bytes = static_cast<const char*>(buffer);
    return transferAll(
        count, [&](std::size_t offset, std::size_t left) { return ::write(fd, bytes + offset, left); });
}

bool sendExact(int fd, const void* buffer, std::size_t count)
{
    const auto* bytes = static_cast<const char*>(buffer);
    return transferAll(count, [&](std::size_t offset, std::size_t left) {
        return ::send(fd, bytes + offset, left, MSG_NOSIGNAL);
    });
}

bool waitReadable(int fd, std::chrono::milliseconds timeout)
{
    pollfd target{fd, POLLIN, 0};
    int ready;
    do {
        ready = ::poll(&target, 1, static_cast<int>(timeout.count()));
    } while (ready < 0 && errno == EINTR);
    return ready > 0;
}

void setBlocking(int fd, bool blocking)
{
    const int flags = ::fcntl(fd, F_GETFL);
    if (flags < 0) {
        throwSystemError("fcntl(F_GETFL)");
    }
    const int updated = blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK);
    if (::fcntl(fd, F_SETFL, updated) < 0) {
        throwSystemError("fcntl(F_SETFL)");
    }
}

}
