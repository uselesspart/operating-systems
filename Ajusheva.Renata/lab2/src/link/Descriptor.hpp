#pragma once

#include <utility>

#include <unistd.h>

namespace chatlab {

class Descriptor {
  public:
    Descriptor() noexcept = default;
    explicit Descriptor(int fd) noexcept : fd_(fd) {}
    ~Descriptor() { reset(); }

    Descriptor(const Descriptor&) = delete;
    Descriptor& operator=(const Descriptor&) = delete;

    Descriptor(Descriptor&& other) noexcept : fd_(other.release()) {}
    Descriptor& operator=(Descriptor&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    int get() const noexcept { return fd_; }
    bool isOpen() const noexcept { return fd_ >= 0; }
    int release() noexcept { return std::exchange(fd_, -1); }

    void reset(int fd = -1) noexcept {
        if (fd_ >= 0) {
            ::close(fd_);
        }
        fd_ = fd;
    }

  private:
    int fd_ = -1;
};

}
