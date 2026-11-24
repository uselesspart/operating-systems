#include "common/named_semaphore.hpp"

#include <cerrno>
#include <fcntl.h>
#include <stdexcept>
#include <system_error>
#include <utility>

namespace lab2::common {
namespace {

void validateName(const std::string& name) {
    if (name.size() < 2 || name.front() != '/' || name.find('/', 1) != std::string::npos ||
        name.find('\0') != std::string::npos) {
        throw std::invalid_argument("Invalid POSIX semaphore name");
    }
}

} // namespace

NamedSemaphore NamedSemaphore::create(std::string name, unsigned int initialValue) {
    validateName(name);
    auto* handle = sem_open(name.c_str(), O_CREAT | O_EXCL, 0600, initialValue);
    if (handle == SEM_FAILED) {
        throw std::system_error(errno, std::generic_category(), "sem_open(create)");
    }
    return NamedSemaphore(std::move(name), handle, true);
}

NamedSemaphore NamedSemaphore::open(std::string name) {
    validateName(name);
    auto* handle = sem_open(name.c_str(), 0);
    if (handle == SEM_FAILED) {
        throw std::system_error(errno, std::generic_category(), "sem_open(open)");
    }
    return NamedSemaphore(std::move(name), handle, false);
}

NamedSemaphore::NamedSemaphore(std::string name, sem_t* handle, bool owner) noexcept
    : name_(std::move(name)), handle_(handle), owner_(owner) {
}

NamedSemaphore::~NamedSemaphore() noexcept {
    cleanup();
}

NamedSemaphore::NamedSemaphore(NamedSemaphore&& other) noexcept
    : name_(std::move(other.name_)), handle_(std::exchange(other.handle_, SEM_FAILED)),
      owner_(std::exchange(other.owner_, false)) {
}

NamedSemaphore& NamedSemaphore::operator=(NamedSemaphore&& other) noexcept {
    if (this != &other) {
        cleanup();
        name_ = std::move(other.name_);
        handle_ = std::exchange(other.handle_, SEM_FAILED);
        owner_ = std::exchange(other.owner_, false);
    }
    return *this;
}

void NamedSemaphore::requireOpen() const {
    if (handle_ == SEM_FAILED) {
        throw std::logic_error("Semaphore is not open");
    }
}

bool NamedSemaphore::wait(const Deadline& deadline) {
    requireOpen();
    // Монотонные часы защищают тайм-аут от изменения системного времени.
    while (sem_clockwait(handle_, CLOCK_MONOTONIC, &deadline.absoluteTime()) == -1) {
        const int error = errno;
        if (error == ETIMEDOUT) {
            return false;
        }
        if (error != EINTR) {
            throw std::system_error(error, std::generic_category(), "sem_clockwait");
        }
    }
    return true;
}

void NamedSemaphore::post() {
    requireOpen();
    if (sem_post(handle_) == -1) {
        throw std::system_error(errno, std::generic_category(), "sem_post");
    }
}

void NamedSemaphore::unlink() {
    requireOpen();
    if (!owner_) {
        throw std::logic_error("Semaphore name is not owned");
    }
    if (sem_unlink(name_.c_str()) == -1 && errno != ENOENT) {
        throw std::system_error(errno, std::generic_category(), "sem_unlink");
    }
    owner_ = false;
}

void NamedSemaphore::cleanup() noexcept {
    const int savedErrno = errno;
    if (owner_) {
        static_cast<void>(sem_unlink(name_.c_str()));
        owner_ = false;
    }
    if (handle_ != SEM_FAILED) {
        static_cast<void>(sem_close(handle_));
        handle_ = SEM_FAILED;
    }
    errno = savedErrno;
}

} // namespace lab2::common
