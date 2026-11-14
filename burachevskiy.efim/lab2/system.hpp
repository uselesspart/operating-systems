#pragma once

#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <unistd.h>

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

[[noreturn]] inline void fail(const char* action) {
    throw std::runtime_error(std::string(action) + ": " + std::strerror(errno));
}

inline void waitFd(int fd, short events, Clock::time_point deadline) {
    for (;;) {
        const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remaining <= 0) throw std::runtime_error("Тайм-аут связи (5 с)");
        pollfd item{fd, events, 0};
        const int result = poll(&item, 1, static_cast<int>(remaining));
        if (result > 0) return;
        if (result == 0) throw std::runtime_error("Тайм-аут связи (5 с)");
        if (errno != EINTR) fail("poll");
    }
}

inline void transfer(int fd, void* buf, std::size_t count, bool writing) {
    const auto deadline = Clock::now() + 5s;
    auto* bytes = static_cast<char*>(buf);
    while (count > 0) {
        waitFd(fd, writing ? POLLOUT : POLLIN, deadline);
        const auto n = writing ? write(fd, bytes, count) : read(fd, bytes, count);
        if (n == 0) throw std::runtime_error("Канал закрыт");
        if (n < 0) {
            if (errno == EINTR || errno == EAGAIN) continue;
            fail(writing ? "write" : "read");
        }
        count -= static_cast<std::size_t>(n);
        bytes += n;
    }
}
