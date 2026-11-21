#include "link/GlobalSemaphore.hpp"

#include "util/Errors.hpp"

#include <cerrno>
#include <ctime>
#include <utility>

#include <fcntl.h>

namespace chatlab {

namespace {

timespec monotonicDeadline(std::chrono::milliseconds timeout) {
    timespec now{};
    ::clock_gettime(CLOCK_MONOTONIC, &now);
    const long long total = now.tv_nsec + static_cast<long long>(timeout.count()) * 1000000LL;
    timespec deadline{};
    deadline.tv_sec = now.tv_sec + static_cast<time_t>(total / 1000000000LL);
    deadline.tv_nsec = static_cast<long>(total % 1000000000LL);
    return deadline;
}

}

GlobalSemaphore::GlobalSemaphore(sem_t* handle, std::string name, bool owner) noexcept
    : handle_(handle), name_(std::move(name)), owner_(owner) {}

GlobalSemaphore GlobalSemaphore::create(const std::string& name) {
    ::sem_unlink(name.c_str());
    sem_t* handle = ::sem_open(name.c_str(), O_CREAT | O_EXCL, 0600, 0);
    if (handle == SEM_FAILED) {
        raiseErrno("sem_open create " + name);
    }
    return GlobalSemaphore(handle, name, true);
}

GlobalSemaphore GlobalSemaphore::attach(const std::string& name) {
    sem_t* handle = ::sem_open(name.c_str(), 0);
    if (handle == SEM_FAILED) {
        raiseErrno("sem_open " + name);
    }
    return GlobalSemaphore(handle, name, false);
}

GlobalSemaphore::GlobalSemaphore(GlobalSemaphore&& other) noexcept
    : handle_(std::exchange(other.handle_, SEM_FAILED)),
      name_(std::move(other.name_)),
      owner_(std::exchange(other.owner_, false)) {}

GlobalSemaphore& GlobalSemaphore::operator=(GlobalSemaphore&& other) noexcept {
    if (this != &other) {
        drop();
        handle_ = std::exchange(other.handle_, SEM_FAILED);
        name_ = std::move(other.name_);
        owner_ = std::exchange(other.owner_, false);
    }
    return *this;
}

GlobalSemaphore::~GlobalSemaphore() {
    drop();
}

void GlobalSemaphore::drop() noexcept {
    if (handle_ != SEM_FAILED) {
        ::sem_close(handle_);
        handle_ = SEM_FAILED;
    }
    if (owner_) {
        ::sem_unlink(name_.c_str());
        owner_ = false;
    }
}

bool GlobalSemaphore::post() {
    return ::sem_post(handle_) == 0;
}

bool GlobalSemaphore::waitFor(std::chrono::milliseconds timeout) {
    const timespec deadline = monotonicDeadline(timeout);
    int result = 0;
    do {
        result = ::sem_clockwait(handle_, CLOCK_MONOTONIC, &deadline);
    } while (result != 0 && errno == EINTR);
    return result == 0;
}

}
