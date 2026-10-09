#include "ipc/Semaphore.h"

#include "util/SystemError.h"

#include <cerrno>
#include <ctime>
#include <utility>

#include <fcntl.h>

namespace chat {

namespace {

timespec deadlineAfter(std::chrono::milliseconds timeout)
{
    timespec now{};
    clock_gettime(CLOCK_REALTIME, &now);
    const long long nanos = now.tv_nsec + (timeout.count() % 1000) * 1'000'000LL;
    timespec deadline{};
    deadline.tv_sec = now.tv_sec + static_cast<time_t>(timeout.count() / 1000 + nanos / 1'000'000'000LL);
    deadline.tv_nsec = static_cast<long>(nanos % 1'000'000'000LL);
    return deadline;
}

}

NamedSemaphore::NamedSemaphore(sem_t* semaphore, std::string name, bool owner) noexcept
    : semaphore_(semaphore), name_(std::move(name)), owner_(owner)
{
}

NamedSemaphore NamedSemaphore::create(const std::string& name)
{
    ::sem_unlink(name.c_str());
    sem_t* semaphore = ::sem_open(name.c_str(), O_CREAT | O_EXCL, 0600, 0);
    if (semaphore == SEM_FAILED) {
        throwSystemError("sem_open(create) " + name);
    }
    return NamedSemaphore(semaphore, name, true);
}

NamedSemaphore NamedSemaphore::open(const std::string& name)
{
    sem_t* semaphore = ::sem_open(name.c_str(), 0);
    if (semaphore == SEM_FAILED) {
        throwSystemError("sem_open " + name);
    }
    return NamedSemaphore(semaphore, name, false);
}

NamedSemaphore::~NamedSemaphore()
{
    release();
}

NamedSemaphore::NamedSemaphore(NamedSemaphore&& other) noexcept
    : semaphore_(std::exchange(other.semaphore_, SEM_FAILED)), name_(std::move(other.name_)),
      owner_(std::exchange(other.owner_, false))
{
}

NamedSemaphore& NamedSemaphore::operator=(NamedSemaphore&& other) noexcept
{
    if (this != &other) {
        release();
        semaphore_ = std::exchange(other.semaphore_, SEM_FAILED);
        name_ = std::move(other.name_);
        owner_ = std::exchange(other.owner_, false);
    }
    return *this;
}

void NamedSemaphore::release() noexcept
{
    if (semaphore_ != SEM_FAILED) {
        ::sem_close(semaphore_);
        semaphore_ = SEM_FAILED;
    }
    if (owner_) {
        ::sem_unlink(name_.c_str());
        owner_ = false;
    }
}

bool NamedSemaphore::post()
{
    return ::sem_post(semaphore_) == 0;
}

bool NamedSemaphore::waitFor(std::chrono::milliseconds timeout)
{
    const timespec deadline = deadlineAfter(timeout);
    int result;
    do {
        result = ::sem_timedwait(semaphore_, &deadline);
    } while (result != 0 && errno == EINTR);
    return result == 0;
}

}
