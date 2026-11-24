#include "common/logger.hpp"

#include <gtest/gtest.h>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

namespace lab2::common {
namespace {

TEST(LoggerTest, ConcurrentMessagesAreCompleteAndUnique) {
    std::ostringstream output;
    Logger logger(output);
    constexpr int threadCount = 4;
    constexpr int messagesPerThread = 25;
    std::vector<std::thread> threads;
    for (int thread = 0; thread < threadCount; ++thread) {
        threads.emplace_back([&, thread] {
            for (int message = 0; message < messagesPerThread; ++message) {
                logger.info(std::to_string(thread) + ":" + std::to_string(message));
            }
        });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    std::set<std::string> expected;
    for (int thread = 0; thread < threadCount; ++thread) {
        for (int message = 0; message < messagesPerThread; ++message) {
            expected.insert("[INFO] " + std::to_string(thread) + ":" + std::to_string(message));
        }
    }
    std::istringstream input(output.str());
    std::string line;
    while (std::getline(input, line)) {
        EXPECT_EQ(expected.erase(line), 1U);
    }
    EXPECT_TRUE(expected.empty());
}

} // namespace
} // namespace lab2::common
