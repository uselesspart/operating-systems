#include "ipc.h"

#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <system_error>
#include <time.h>

void check_stop(const std::atomic_bool& stop) {
    if (stop.load()) {
        throw std::runtime_error("Cancelled");
    }
}

bool process_alive(int fd) {
    if (fd < 0) {
        return true;
    }
    pollfd event{fd, POLLIN, 0};
    int result;
    do {
        result = poll(&event, 1, 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        system_error("Cannot check process");
    }
    return result == 0;
}

int remaining_ms(Deadline deadline, int limit) {
    auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    return static_cast<int>(std::clamp<long long>(left.count(), 0, limit));
}

bool can_write(int fd) {
    pollfd event{fd, POLLOUT, 0};
    int ready = poll(&event, 1, 0);
    if (ready < 0 && errno != EINTR) {
        system_error("Cannot check output");
    }
    return ready > 0;
}

std::string queue_name(const std::string& directory, const std::string& suffix) {
    return "/" + directory.substr(directory.find_last_of('/') + 1) + "_" + suffix;
}

void system_error(const std::string& text) {
    throw std::system_error(errno, std::generic_category(), text);
}

NamedSemaphore::~NamedSemaphore() {
    if (sem != SEM_FAILED) {
        sem_close(sem);
    }
    if (owner) {
        sem_unlink(name.c_str());
    }
}

void NamedSemaphore::open(const std::string& next, bool create, unsigned value) {
    name = next;
    sem = create ? sem_open(name.c_str(), O_CREAT | O_EXCL, 0600, value)
                 : sem_open(name.c_str(), 0);
    if (sem == SEM_FAILED) {
        system_error("Cannot open semaphore");
    }
    owner = create;
}

bool NamedSemaphore::try_wait() {
    while (sem_trywait(sem) != 0) {
        if (errno == EAGAIN) {
            return false;
        }
        if (errno != EINTR) {
            system_error("Cannot wait for semaphore");
        }
    }
    return true;
}

void NamedSemaphore::wait(Deadline deadline, const std::atomic_bool& stop, int peer_fd) {
    while (true) {
        check_stop(stop);
        if (try_wait()) {
            return;
        }
        if (!process_alive(peer_fd)) {
            throw ConnectionClosed();
        }
        if (Clock::now() >= deadline) {
            throw std::runtime_error("Semaphore timeout");
        }
        timespec now{};
        if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
            system_error("Cannot read clock");
        }
        long nanos = now.tv_nsec + static_cast<long>(remaining_ms(deadline)) * 1000000L;
        timespec until{now.tv_sec + nanos / 1000000000L, nanos % 1000000000L};
        if (sem_clockwait(sem, CLOCK_MONOTONIC, &until) == 0) {
            return;
        }
        if (errno != EINTR && errno != ETIMEDOUT) {
            system_error("Cannot wait for semaphore");
        }
    }
}

void NamedSemaphore::post() {
    if (sem_post(sem) != 0) {
        system_error("Cannot post semaphore");
    }
}

void NamedSemaphore::post_on_exit() noexcept {
    if (sem != SEM_FAILED) {
        sem_post(sem);
    }
}
