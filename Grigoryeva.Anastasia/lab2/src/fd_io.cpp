#include "fd_io.h"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>

namespace {

std::atomic<int> g_timeoutMs{0};

int RemainingMs(std::chrono::steady_clock::time_point deadline) {
    using namespace std::chrono;
    long long left = duration_cast<milliseconds>(deadline - steady_clock::now()).count();
    return left > 0 ? static_cast<int>(left) : 0;
}

}  // namespace

int IoTimeoutMs() {
    int v = g_timeoutMs.load();
    if (v > 0) return v;
    const char* env = std::getenv("WOLF_IO_TIMEOUT_MS");
    int parsed = env ? std::atoi(env) : 0;
    v = parsed > 0 ? parsed : 5000;
    g_timeoutMs.store(v);
    return v;
}

void SetIoTimeoutMs(int ms) { g_timeoutMs.store(ms > 0 ? ms : 5000); }

void UniqueFd::reset(int fd) {
    if (fd_ >= 0) ::close(fd_);
    fd_ = fd;
}

bool ReadFdExact(int fd, void* buf, size_t count, int timeoutMs, std::string& err) {
    char* p = static_cast<char*>(buf);
    size_t got = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (got < count) {
        struct pollfd pfd = {fd, POLLIN, 0};
        int r = ::poll(&pfd, 1, RemainingMs(deadline));
        if (r < 0) {
            if (errno == EINTR) continue;
            err = std::string("poll: ") + std::strerror(errno);
            return false;
        }
        if (r == 0) {
            err = "таймаут чтения (" + std::to_string(timeoutMs) + " мс)";
            return false;
        }
        ssize_t n = ::read(fd, p + got, count - got);
        if (n > 0) {
            got += static_cast<size_t>(n);
        } else if (n == 0) {
            err = "соединение закрыто собеседником";
            return false;
        } else if (errno != EINTR && errno != EAGAIN) {
            err = std::string("read: ") + std::strerror(errno);
            return false;
        }
    }
    return true;
}

bool WriteFdAll(int fd, const void* buf, size_t count, int timeoutMs, bool isSocket, std::string& err) {
    const char* p = static_cast<const char*>(buf);
    size_t sent = 0;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (sent < count) {
        struct pollfd pfd = {fd, POLLOUT, 0};
        int r = ::poll(&pfd, 1, RemainingMs(deadline));
        if (r < 0) {
            if (errno == EINTR) continue;
            err = std::string("poll: ") + std::strerror(errno);
            return false;
        }
        if (r == 0) {
            err = "таймаут записи (" + std::to_string(timeoutMs) + " мс)";
            return false;
        }
        ssize_t n = isSocket ? ::send(fd, p + sent, count - sent, MSG_NOSIGNAL) : ::write(fd, p + sent, count - sent);
        if (n >= 0) {
            sent += static_cast<size_t>(n);
        } else if (errno == EPIPE || errno == ECONNRESET) {
            err = "соединение закрыто собеседником";
            return false;
        } else if (errno != EINTR && errno != EAGAIN) {
            err = std::string("write: ") + std::strerror(errno);
            return false;
        }
    }
    return true;
}
