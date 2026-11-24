#include "common/deadline.hpp"
#include "handshake/handshake_client.hpp"
#include "handshake/handshake_server.hpp"
#include "handshake/handshake_signals.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <gtest/gtest.h>
#include <memory>
#include <pthread.h>
#include <stdexcept>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

namespace lab2::handshake {
namespace {

using namespace std::chrono_literals;

void prepareSignals() {
    blockHandshakeSignals();
    sigset_t signals{};
    sigemptyset(&signals);
    sigaddset(&signals, SIGUSR1);
    sigaddset(&signals, SIGUSR2);
    const timespec immediate{};
    while (sigtimedwait(&signals, nullptr, &immediate) >= 0) {
    }
}

// Дочерние процессы используются только в тестах, чтобы проверять реальные
// сигналы ядра без запуска GUI. При падении теста процессы не остаются в фоне.
class Child {
public:
    explicit Child(const std::function<bool()>& task) : pid_(fork()) {
        if (pid_ == -1) {
            throw std::system_error(errno, std::generic_category(), "fork");
        }
        if (pid_ == 0) {
            try {
                _exit(task() ? EXIT_SUCCESS : EXIT_FAILURE);
            } catch (...) {
                _exit(EXIT_FAILURE);
            }
        }
    }

    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;

    ~Child() {
        if (pid_ > 0) {
            kill(pid_, SIGKILL);
            while (waitpid(pid_, nullptr, 0) == -1 && errno == EINTR) {
            }
        }
    }

    pid_t pid() const noexcept {
        return pid_;
    }

    bool succeeded() {
        const common::Deadline deadline(6s);
        while (deadline.remainingMilliseconds() > 0) {
            int status{};
            const pid_t result = waitpid(pid_, &status, WNOHANG);
            if (result == pid_) {
                pid_ = -1;
                return WIFEXITED(status) && WEXITSTATUS(status) == EXIT_SUCCESS;
            }
            if (result == -1 && errno != EINTR) {
                throw std::system_error(errno, std::generic_category(), "waitpid");
            }
            std::this_thread::sleep_for(5ms);
        }
        return false;
    }

private:
    pid_t pid_;
};

TEST(HandshakeTest, ReturnsAssignedIdAfterPreparation) {
    prepareSignals();
    const pid_t host = getpid();
    Child child([host] { return HandshakeClient{}.connect(host) == 42U; });
    HandshakeServer server;
    const auto result = server.handleNext([&](pid_t pid) {
        EXPECT_EQ(pid, child.pid());
        return 42U;
    });
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->clientPid, child.pid());
    EXPECT_EQ(result->clientId, 42U);
    EXPECT_TRUE(result->newSession);
    EXPECT_FALSE(result->replyError);
    EXPECT_TRUE(child.succeeded());
}

TEST(HandshakeTest, DuplicateRequestReusesSession) {
    prepareSignals();
    const pid_t host = getpid();
    Child child([host] {
        sigset_t replies{};
        sigemptyset(&replies);
        sigaddset(&replies, SIGUSR2);
        for (int i = 0; i < 2; ++i) {
            if (kill(host, SIGUSR1) == -1)
                return false;
            const timespec wait{2, 0};
            siginfo_t info{};
            if (sigtimedwait(&replies, &info, &wait) != SIGUSR2 || info.si_pid != host ||
                info.si_value.sival_int != 17)
                return false;
        }
        return true;
    });
    HandshakeServer server;
    int calls = 0;
    const auto prepare = [&](pid_t) {
        ++calls;
        return 17U;
    };
    const auto first = server.handleNext(prepare, 2s);
    const auto second = server.handleNext(prepare, 2s);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_TRUE(first->newSession);
    EXPECT_FALSE(second->newSession);
    EXPECT_EQ(second->clientId, first->clientId);
    EXPECT_EQ(calls, 1);
    EXPECT_TRUE(child.succeeded());
}

TEST(HandshakeTest, ConcurrentClientsReceiveDistinctIds) {
    prepareSignals();
    const pid_t host = getpid();
    std::vector<std::unique_ptr<Child>> children;
    for (std::uint32_t i = 1; i <= 6; ++i) {
        children.push_back(
            std::make_unique<Child>([host, i] { return HandshakeClient{}.connect(host) == i; }));
    }
    HandshakeServer server;
    std::size_t accepted = 0;
    const common::Deadline deadline(4s);
    while (accepted < children.size() && deadline.remainingMilliseconds() > 0) {
        const auto result = server.handleNext(
            [&](pid_t pid) -> std::uint32_t {
                for (std::size_t i = 0; i < children.size(); ++i) {
                    if (children[i]->pid() == pid)
                        return static_cast<std::uint32_t>(i + 1);
                }
                throw std::logic_error("Unknown client");
            },
            100ms);
        if (result && result->newSession) {
            EXPECT_FALSE(result->replyError);
            ++accepted;
        }
    }
    ASSERT_EQ(accepted, children.size());
    for (const auto& child : children)
        EXPECT_TRUE(child->succeeded());
}

TEST(HandshakeTest, ClientTimesOutWithoutReply) {
    prepareSignals();
    const pid_t host = getpid();
    Child child([host] {
        try {
            static_cast<void>(HandshakeClient{}.connect(host, 250ms));
        } catch (const std::system_error& error) {
            return error.code().value() == ETIMEDOUT;
        }
        return false;
    });
    EXPECT_TRUE(child.succeeded());
}

TEST(HandshakeTest, PreparationFailureDoesNotSendSuccess) {
    prepareSignals();
    const pid_t host = getpid();
    Child child([host] {
        try {
            static_cast<void>(HandshakeClient{}.connect(host, 250ms));
        } catch (const std::system_error& error) {
            return error.code().value() == ETIMEDOUT;
        }
        return false;
    });
    HandshakeServer server;
    EXPECT_THROW(static_cast<void>(server.handleNext([](pid_t) -> std::uint32_t {
                     throw std::runtime_error("Cannot prepare session");
                 })),
                 std::runtime_error);
    EXPECT_TRUE(child.succeeded());
}

TEST(HandshakeTest, IgnoresReplyFromWrongProcess) {
    prepareSignals();
    const pid_t host = getpid();
    Child child([host] {
        union sigval value{};
        value.sival_int = 99;
        if (sigqueue(getpid(), SIGUSR2, value) == -1) {
            return false;
        }
        return HandshakeClient{}.connect(host, 2s) == 12U;
    });
    HandshakeServer server;
    const common::Deadline deadline(1s);
    while (deadline.remainingMilliseconds() > 0) {
        const auto result = server.handleNext([](pid_t) { return 12U; }, 100ms);
        if (result && result->replyError.value() == ESRCH) {
            break;
        }
    }
    EXPECT_TRUE(child.succeeded());
}

} // namespace
} // namespace lab2::handshake
