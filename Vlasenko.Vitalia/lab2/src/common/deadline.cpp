#include "common/deadline.hpp"

#include <cerrno>
#include <climits>
#include <cstdint>
#include <stdexcept>
#include <system_error>

namespace lab2::common {
namespace {

constexpr long kNanosecondsPerSecond = 1000000000L;
constexpr long kNanosecondsPerMillisecond = 1000000L;

timespec monotonicNow() {
    timespec now{};
    if (clock_gettime(CLOCK_MONOTONIC, &now) == -1) {
        throw std::system_error(errno, std::generic_category(), "clock_gettime");
    }
    return now;
}

} // namespace

Deadline::Deadline(std::chrono::milliseconds timeout) {
    if (timeout.count() < 0 || timeout.count() > INT_MAX) {
        throw std::invalid_argument("Timeout is outside the supported range");
    }

    end_ = monotonicNow();
    end_.tv_sec += timeout.count() / 1000;
    end_.tv_nsec += (timeout.count() % 1000) * kNanosecondsPerMillisecond;
    if (end_.tv_nsec >= kNanosecondsPerSecond) {
        ++end_.tv_sec;
        end_.tv_nsec -= kNanosecondsPerSecond;
    }
}

const timespec& Deadline::absoluteTime() const noexcept {
    return end_;
}

int Deadline::remainingMilliseconds() const {
    const auto now = monotonicNow();
    const auto remaining =
        static_cast<std::int64_t>(end_.tv_sec - now.tv_sec) * kNanosecondsPerSecond + end_.tv_nsec -
        now.tv_nsec;
    if (remaining <= 0) {
        return 0;
    }

    // poll принимает миллисекунды: округление вверх не сокращает срок ожидания.
    return static_cast<int>((remaining + kNanosecondsPerMillisecond - 1) /
                            kNanosecondsPerMillisecond);
}

} // namespace lab2::common
