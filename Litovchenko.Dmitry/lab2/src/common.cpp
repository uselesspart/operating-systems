#include "common.h"

#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <stdexcept>

namespace {
const char* g_logName = "wolf";
}

Shared* createShared() {
    void* mem = mmap(nullptr, sizeof(Shared), PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) throw std::runtime_error(std::string("mmap: ") + std::strerror(errno));
    auto* shared = static_cast<Shared*>(mem);
    if (sem_init(&shared->hostCanRead, 1, 0) < 0 || sem_init(&shared->goatCanRead, 1, 0) < 0) {
        throw std::runtime_error(std::string("sem_init: ") + std::strerror(errno));
    }
    return shared;
}

void destroyShared(Shared* shared) {
    sem_destroy(&shared->hostCanRead);
    sem_destroy(&shared->goatCanRead);
    munmap(shared, sizeof(Shared));
}

bool waitSem(sem_t* sem, int seconds) {
    timespec deadline;
    clock_gettime(CLOCK_REALTIME, &deadline);
    deadline.tv_sec += seconds;
    while (sem_timedwait(sem, &deadline) < 0) {
        if (errno != EINTR) return false;
    }
    return true;
}

bool retryWithTimeout(const std::function<bool()>& fn) {
    timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (;;) {
        if (fn()) return true;
        timespec now;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec - start.tv_sec >= kTimeoutSec) return false;
        timespec pause{0, 20 * 1000 * 1000};
        nanosleep(&pause, nullptr);
    }
}

bool readAll(int fd, void* buf, size_t count) {
    auto* p = static_cast<char*>(buf);
    while (count > 0) {
        pollfd pfd{fd, POLLIN, 0};
        int ready = poll(&pfd, 1, kTimeoutSec * 1000);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) return false;
        ssize_t n = read(fd, p, count);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        count -= static_cast<size_t>(n);
    }
    return true;
}

bool writeAll(int fd, const void* buf, size_t count) {
    const auto* p = static_cast<const char*>(buf);
    while (count > 0) {
        pollfd pfd{fd, POLLOUT, 0};
        int ready = poll(&pfd, 1, kTimeoutSec * 1000);
        if (ready < 0 && errno == EINTR) continue;
        if (ready <= 0) return false;
        ssize_t n = write(fd, p, count);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) return false;
        p += n;
        count -= static_cast<size_t>(n);
    }
    return true;
}

void setLogName(const char* name) {
    g_logName = name;
}

void logMsg(const char* fmt, ...) {
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    tm local;
    localtime_r(&ts.tv_sec, &local);
    char text[512];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(text, sizeof(text), fmt, args);
    va_end(args);
    std::fprintf(stderr, "%02d:%02d:%02d.%03ld %s[%d]: %s\n", local.tm_hour, local.tm_min, local.tm_sec,
                 ts.tv_nsec / 1000000, g_logName, getpid(), text);
}
