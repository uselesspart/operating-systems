#include "common/unique_fd.hpp"

#include <cerrno>
#include <unistd.h>
#include <utility>

namespace lab2::common {

UniqueFd::UniqueFd(int fd) noexcept : fd_(fd) {
}

UniqueFd::~UniqueFd() noexcept {
    reset();
}

UniqueFd::UniqueFd(UniqueFd&& other) noexcept : fd_(other.release()) {
}

UniqueFd& UniqueFd::operator=(UniqueFd&& other) noexcept {
    if (this != &other) {
        reset(other.release());
    }
    return *this;
}

int UniqueFd::get() const noexcept {
    return fd_;
}

UniqueFd::operator bool() const noexcept {
    return fd_ >= 0;
}

int UniqueFd::release() noexcept {
    return std::exchange(fd_, -1);
}

void UniqueFd::reset(int fd) noexcept {
    if (fd_ == fd) {
        return;
    }
    if (fd_ >= 0) {
        const int savedErrno = errno;
        // В Linux повторный close после EINTR может закрыть уже переиспользованный fd.
        static_cast<void>(close(fd_));
        errno = savedErrno;
    }
    fd_ = fd;
}

} // namespace lab2::common
