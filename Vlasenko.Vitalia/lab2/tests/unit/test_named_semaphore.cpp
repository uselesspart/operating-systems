#include "common/named_semaphore.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <sys/wait.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace lab2::common {
namespace {

using namespace std::chrono_literals;

std::string uniqueName() {
    static std::atomic<unsigned int> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    return "/lab2_test_" + std::to_string(getpid()) + "_" + std::to_string(stamp) + "_" +
           std::to_string(counter++);
}

TEST(NamedSemaphoreTest, WaitTimesOut) {
    auto semaphore = NamedSemaphore::create(uniqueName());
    const auto start = std::chrono::steady_clock::now();
    EXPECT_FALSE(semaphore.wait(Deadline(50ms)));
    EXPECT_GE(std::chrono::steady_clock::now() - start, 50ms);
}

TEST(NamedSemaphoreTest, RejectsDuplicateCreation) {
    const auto name = uniqueName();
    auto owner = NamedSemaphore::create(name, 1);
    EXPECT_THROW(static_cast<void>(NamedSemaphore::create(name)), std::system_error);
    EXPECT_TRUE(owner.wait(Deadline(0ms)));
}

TEST(NamedSemaphoreTest, PeerCannotRemoveOwnedName) {
    const auto name = uniqueName();
    auto owner = NamedSemaphore::create(name);
    {
        auto peer = NamedSemaphore::open(name);
        EXPECT_THROW(peer.unlink(), std::logic_error);
    }
    auto nextPeer = NamedSemaphore::open(name);
    nextPeer.post();
    EXPECT_TRUE(owner.wait(Deadline(0ms)));
}

TEST(NamedSemaphoreTest, OwnerDestructionRemovesName) {
    const auto name = uniqueName();
    {
        auto owner = NamedSemaphore::create(name);
    }
    EXPECT_THROW(static_cast<void>(NamedSemaphore::open(name)), std::system_error);
}

TEST(NamedSemaphoreTest, SynchronizesSeparateProcesses) {
    const auto name = uniqueName();
    auto owner = NamedSemaphore::create(name);
    const pid_t pid = fork();
    ASSERT_GE(pid, 0);
    if (pid == 0) {
        try {
            auto peer = NamedSemaphore::open(name);
            peer.post();
            _exit(0);
        } catch (...) {
            _exit(1);
        }
    }
    // Сначала собираем статус ребёнка, даже если последующая проверка не пройдёт.
    int status = 0;
    pid_t waited;
    do {
        waited = waitpid(pid, &status, 0);
    } while (waited == -1 && errno == EINTR);
    ASSERT_EQ(waited, pid);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
    EXPECT_TRUE(owner.wait(Deadline(100ms)));
}

} // namespace
} // namespace lab2::common
