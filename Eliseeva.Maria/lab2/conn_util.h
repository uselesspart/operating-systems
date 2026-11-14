#pragma once
// Вспомогательные функции ввода-вывода с тайм-аутами для conn_*.cpp.
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstddef>

namespace connutil {

inline long long now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

inline bool set_nonblock(int fd) {
    int fl = fcntl(fd, F_GETFL, 0);
    return fl >= 0 && fcntl(fd, F_SETFL, fl | O_NONBLOCK) == 0;
}

inline int wait_ms(long long deadline, int timeout_ms) {
    if (timeout_ms < 0) return -1;
    long long left = deadline - now_ms();
    return left < 0 ? 0 : static_cast<int>(left);
}

// 1 — прочитано count байт, 0 — тайм-аут до первого байта, -1 — ошибка/EOF/тайм-аут посреди сообщения
inline int read_all(int fd, void* buf, size_t count, int timeout_ms) {
    char* p = static_cast<char*>(buf);
    size_t got = 0;
    const long long deadline = now_ms() + (timeout_ms > 0 ? timeout_ms : 0);
    while (got < count) {
        pollfd pf{fd, POLLIN, 0};
        int r = poll(&pf, 1, wait_ms(deadline, timeout_ms));
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (r == 0) return got == 0 ? 0 : -1;
        ssize_t k = read(fd, p + got, count - got);
        if (k > 0) {
            got += static_cast<size_t>(k);
        } else if (k == 0) {
            return -1;
        } else if (errno != EAGAIN && errno != EINTR) {
            return -1;
        }
    }
    return 1;
}

inline bool write_all(int fd, const void* buf, size_t count, int timeout_ms) {
    const char* p = static_cast<const char*>(buf);
    size_t sent = 0;
    const long long deadline = now_ms() + (timeout_ms > 0 ? timeout_ms : 0);
    while (sent < count) {
        pollfd pf{fd, POLLOUT, 0};
        int r = poll(&pf, 1, wait_ms(deadline, timeout_ms));
        if (r < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (r == 0) return false;  // тайм-аут
        ssize_t k = write(fd, p + sent, count - sent);
        if (k > 0) {
            sent += static_cast<size_t>(k);
        } else if (k < 0 && errno != EAGAIN && errno != EINTR) {
            return false;
        }
    }
    return true;
}

inline void close_fd(int& fd) {
    if (fd >= 0) ::close(fd);
    fd = -1;
}

}  // namespace connutil
