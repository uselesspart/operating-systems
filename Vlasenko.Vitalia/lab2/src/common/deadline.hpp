#pragma once

#include "common/constants.hpp"

#include <ctime>

namespace lab2::common {

class Deadline {
public:
    // Нулевой тайм-аут разрешает только немедленную попытку без ожидания.
    explicit Deadline(std::chrono::milliseconds timeout = kIpcTimeout);

    [[nodiscard]] const timespec& absoluteTime() const noexcept;
    [[nodiscard]] int remainingMilliseconds() const;

private:
    timespec end_{};
};

} // namespace lab2::common
