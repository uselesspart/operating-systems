#include "common/deadline.hpp"

#include <chrono>
#include <climits>
#include <gtest/gtest.h>
#include <stdexcept>
#include <thread>

namespace lab2::common {
namespace {

using namespace std::chrono_literals;

TEST(DeadlineTest, DefaultTimeoutIsFiveSeconds) {
    EXPECT_EQ(kIpcTimeout, 5000ms);
    const Deadline deadline;
    EXPECT_GT(deadline.remainingMilliseconds(), 0);
    EXPECT_LE(deadline.remainingMilliseconds(), 5000);
}

TEST(DeadlineTest, RemainingTimeDoesNotRestart) {
    const Deadline deadline(30ms);
    std::this_thread::sleep_for(50ms);
    EXPECT_EQ(deadline.remainingMilliseconds(), 0);
    EXPECT_EQ(deadline.remainingMilliseconds(), 0);
}

} // namespace
} // namespace lab2::common
