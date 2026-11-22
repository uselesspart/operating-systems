#pragma once

#include <unistd.h>

namespace disk_monitor {

class UniqueFd {
public:
    UniqueFd() = default;
    explicit UniqueFd(int descriptor) noexcept : descriptor_(descriptor) {}
    ~UniqueFd() { reset(); }

    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    UniqueFd(UniqueFd&& other) noexcept : descriptor_(other.release()) {}
    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(other.release());
        }
        return *this;
    }

    int get() const noexcept { return descriptor_; }
    explicit operator bool() const noexcept { return descriptor_ >= 0; }

    int release() noexcept {
        const int result = descriptor_;
        descriptor_ = -1;
        return result;
    }

    void reset(int descriptor = -1) noexcept {
        if (descriptor_ >= 0) {
            close(descriptor_);
        }
        descriptor_ = descriptor;
    }

private:
    int descriptor_ = -1;
};

}  // namespace disk_monitor

