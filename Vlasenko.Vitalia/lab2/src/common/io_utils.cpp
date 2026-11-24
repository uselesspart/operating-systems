#include "common/io_utils.hpp"

#include <algorithm>
#include <cerrno>
#include <climits>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <signal.h>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace lab2::common {
namespace {

[[noreturn]] void fail(int error, const char* operation) {
    throw std::system_error(error, std::generic_category(), operation);
}

int getFlags(int fd, int command) {
    int flags;
    do {
        flags = fcntl(fd, command);
    } while (flags == -1 && errno == EINTR);
    if (flags == -1) {
        fail(errno, "fcntl(get)");
    }
    return flags;
}

void setFlags(int fd, int command, int flags) {
    int result;
    do {
        result = fcntl(fd, command, flags);
    } while (result == -1 && errno == EINTR);
    if (result == -1) {
        fail(errno, "fcntl(set)");
    }
}

void validateIo(int fd, const void* buffer) {
    if (buffer == nullptr) {
        throw std::invalid_argument("Null I/O buffer");
    }
    if ((getFlags(fd, F_GETFL) & O_NONBLOCK) == 0) {
        throw std::invalid_argument("I/O descriptor must be nonblocking");
    }
}

void waitReady(int fd, short events, const Deadline& deadline) {
    pollfd descriptor{fd, events, 0};
    for (;;) {
        const int result = poll(&descriptor, 1, deadline.remainingMilliseconds());
        if (result == 0) {
            fail(ETIMEDOUT, "poll");
        }
        if (result == -1) {
            if (errno == EINTR) {
                if (deadline.remainingMilliseconds() == 0) {
                    fail(ETIMEDOUT, "poll");
                }
                continue;
            }
            fail(errno, "poll");
        }
        if ((descriptor.revents & POLLNVAL) != 0) {
            fail(EBADF, "poll");
        }
        // POLLHUP может сопровождать ещё не прочитанные данные; read определит EOF.
        if ((descriptor.revents & (events | POLLERR | POLLHUP)) != 0) {
            return;
        }
        if (deadline.remainingMilliseconds() == 0) {
            fail(ETIMEDOUT, "poll");
        }
    }
}

class SigpipeGuard {
public:
    SigpipeGuard() {
        sigemptyset(&set_);
        sigaddset(&set_, SIGPIPE);
        const int error = pthread_sigmask(SIG_BLOCK, &set_, &oldMask_);
        if (error != 0) {
            fail(error, "pthread_sigmask");
        }
        sigset_t pending{};
        if (sigpending(&pending) == -1) {
            const int pendingError = errno;
            static_cast<void>(pthread_sigmask(SIG_SETMASK, &oldMask_, nullptr));
            fail(pendingError, "sigpending");
        }
        wasPending_ = sigismember(&pending, SIGPIPE) == 1;
    }

    ~SigpipeGuard() noexcept {
        const int savedErrno = errno;
        if (brokenPipe_ && !wasPending_) {
            // Удаляем только сигнал от нашей записи, не затрагивая ранее ожидавший SIGPIPE.
            const timespec noWait{};
            while (sigtimedwait(&set_, nullptr, &noWait) == -1 && errno == EINTR) {
            }
        }
        static_cast<void>(pthread_sigmask(SIG_SETMASK, &oldMask_, nullptr));
        errno = savedErrno;
    }

    SigpipeGuard(const SigpipeGuard&) = delete;
    SigpipeGuard& operator=(const SigpipeGuard&) = delete;

    void markBrokenPipe() noexcept {
        brokenPipe_ = true;
    }

private:
    sigset_t set_{};
    sigset_t oldMask_{};
    bool wasPending_{};
    bool brokenPipe_{};
};

template <typename Transfer>
void transferExact(int fd, std::size_t count, short events, const Deadline& deadline,
                   Transfer transfer) {
    std::size_t offset = 0;
    bool first = true;
    while (offset < count) {
        if (!first && deadline.remainingMilliseconds() == 0) {
            fail(ETIMEDOUT, "I/O deadline");
        }
        first = false;
        waitReady(fd, events, deadline);
        const auto chunk = std::min(count - offset, static_cast<std::size_t>(SSIZE_MAX));
        const ssize_t result = transfer(offset, chunk);
        if (result > 0) {
            offset += static_cast<std::size_t>(result);
        } else if (result == 0) {
            fail(events == POLLIN ? ECONNRESET : EIO,
                 events == POLLIN ? "Unexpected end of stream" : "write returned zero");
        } else {
            const int error = errno;
            if (error != EINTR && error != EAGAIN && error != EWOULDBLOCK) {
                fail(error, events == POLLIN ? "read" : "write");
            }
        }
    }
}

} // namespace

void setNonBlocking(int fd) {
    setFlags(fd, F_SETFL, getFlags(fd, F_GETFL) | O_NONBLOCK);
}

void setCloseOnExec(int fd) {
    setFlags(fd, F_SETFD, getFlags(fd, F_GETFD) | FD_CLOEXEC);
}

void readExact(int fd, void* buffer, std::size_t count, const Deadline& deadline) {
    if (count == 0) {
        return;
    }
    validateIo(fd, buffer);
    auto* bytes = static_cast<unsigned char*>(buffer);
    transferExact(fd, count, POLLIN, deadline, [&](std::size_t offset, std::size_t chunk) {
        return read(fd, bytes + offset, chunk);
    });
}

void writeExact(int fd, const void* buffer, std::size_t count, const Deadline& deadline) {
    if (count == 0) {
        return;
    }
    validateIo(fd, buffer);
    SigpipeGuard guard;
    const auto* bytes = static_cast<const unsigned char*>(buffer);
    transferExact(fd, count, POLLOUT, deadline, [&](std::size_t offset, std::size_t chunk) {
        const auto result = write(fd, bytes + offset, chunk);
        if (result == -1 && errno == EPIPE) {
            guard.markBrokenPipe();
        }
        return result;
    });
}

} // namespace lab2::common
