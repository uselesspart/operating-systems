#include "client/client_controller.hpp"
#include "common/io_utils.hpp"
#include "common/unique_fd.hpp"
#include "handshake/handshake_client.hpp"
#include "handshake/handshake_signals.hpp"
#include "host/host_controller.hpp"
#include "ipc/connection_factory.hpp"
#include "protocol/protocol.hpp"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <functional>
#include <gtest/gtest.h>
#include <sstream>
#include <stdexcept>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>

namespace lab2::host {
namespace {
using namespace std::chrono_literals;
using game::KidState;
using protocol::MessageType;

enum class ChildMode { Normal, Silent, Disconnect, WrongRound, InvalidDeadMove };

void runChild(pid_t hostPid, int number, ChildMode mode) {
    const auto id = handshake::HandshakeClient{}.connect(hostPid);
    auto connection = ipc::connectClientConnection(hostPid, id);
    if (mode == ChildMode::Normal) {
        std::ostringstream output;
        common::Logger logger(output);
        client::ClientController controller(logger);
        controller.play(*connection, id, [number](KidState) { return number; });
        return;
    }
    protocol::sendMessage(*connection, {MessageType::ClientReady, id});
    unsigned int rounds = 0;
    for (;;) {
        const auto message = protocol::receiveMessage(*connection);
        if (message.type == MessageType::Disconnect || message.type == MessageType::GameOver) {
            return;
        }
        if (message.type != MessageType::RoundStart) {
            continue;
        }
        ++rounds;
        if (mode == ChildMode::Silent) {
            std::this_thread::sleep_for(6s);
            return;
        }
        if (mode == ChildMode::Disconnect) {
            protocol::sendMessage(*connection, {MessageType::Disconnect, id});
            return;
        }
        if (mode == ChildMode::WrongRound) {
            protocol::sendMessage(*connection,
                                  {MessageType::ClientMove, id, message.roundNumber + 1, 1});
            return;
        }
        protocol::sendMessage(*connection, {MessageType::ClientMove, id, message.roundNumber,
                                            static_cast<std::uint16_t>(rounds == 1 ? 1 : 51)});
        if (rounds == 2) {
            return;
        }
    }
}

// fork применяется только тестами до запуска потоков хоста. IPC-ресурсы
// создаются позже, при handshake: клиент не получает их по наследству.
class Child {
public:
    Child(int number, ChildMode mode = ChildMode::Normal, bool delayed = false) {
        int fds[2];
        if (pipe(fds) == -1) {
            throw std::system_error(errno, std::generic_category(), "pipe");
        }
        common::UniqueFd reader(fds[0]);
        common::UniqueFd writer(fds[1]);
        const pid_t hostPid = getpid();
        pid_ = fork();
        if (pid_ == -1) {
            throw std::system_error(errno, std::generic_category(), "fork");
        }
        if (pid_ == 0) {
            writer.reset();
            try {
                if (delayed) {
                    char start{};
                    ssize_t received;
                    do {
                        received = read(reader.get(), &start, 1);
                    } while (received == -1 && errno == EINTR);
                    if (received != 1) {
                        _exit(2);
                    }
                }
                reader.reset();
                runChild(hostPid, number, mode);
                _exit(0);
            } catch (...) {
                _exit(1);
            }
        }
        reader.reset();
        gate_ = std::move(writer);
    }

    ~Child() {
        if (pid_ > 0) {
            kill(pid_, SIGKILL);
            while (waitpid(pid_, nullptr, 0) == -1 && errno == EINTR) {
            }
        }
    }
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;

    [[nodiscard]] pid_t pid() const noexcept {
        return pid_;
    }

    void release() {
        const char start = 'S';
        common::setNonBlocking(gate_.get());
        common::writeExact(gate_.get(), &start, 1);
        gate_.reset();
    }

    int exitCode() {
        const auto deadline = std::chrono::steady_clock::now() + 8s;
        while (std::chrono::steady_clock::now() < deadline) {
            int status{};
            const auto result = waitpid(pid_, &status, WNOHANG);
            if (result == pid_) {
                pid_ = -1;
                return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            }
            if (result == -1 && errno != EINTR) {
                throw std::system_error(errno, std::generic_category(), "waitpid");
            }
            std::this_thread::sleep_for(10ms);
        }
        throw std::runtime_error("Child exit timeout");
    }

private:
    pid_t pid_{-1};
    common::UniqueFd gate_;
};

class Events {
public:
    explicit Events(HostController& host) : host_(host) {
    }

    HostEvent wait(HostEventType type, std::function<bool(const HostEvent&)> match = {}) {
        const auto deadline = std::chrono::steady_clock::now() + 8s;
        while (std::chrono::steady_clock::now() < deadline) {
            auto incoming = host_.drainEvents();
            for (auto& event : incoming) {
                events_.push_back(std::move(event));
            }
            const auto found =
                std::find_if(events_.begin(), events_.end(), [&](const HostEvent& event) {
                    return event.type == type && (!match || match(event));
                });
            if (found != events_.end()) {
                auto event = std::move(*found);
                events_.erase(found);
                return event;
            }
            std::this_thread::sleep_for(10ms);
        }
        throw std::runtime_error("Host event timeout");
    }

    HostEvent round(HostEventType type, std::uint64_t number) {
        return wait(type, [number](const HostEvent& event) { return event.roundNumber == number; });
    }

private:
    HostController& host_;
    std::vector<HostEvent> events_;
};

TEST(HostControllerTest, TwoDeadRoundsEndGame) {
    handshake::blockHandshakeSignals();
    Child child(1);
    HostController host;
    host.start();
    Events events(host);
    events.round(HostEventType::RoundStarted, 1);
    EXPECT_FALSE(host.submitWolfNumber(2, 100));
    EXPECT_GT(host.inputTimeRemaining(1), 0ms);
    EXPECT_LE(host.inputTimeRemaining(1), 3000ms);
    EXPECT_FALSE(host.submitWolfNumber(1, 0));
    EXPECT_FALSE(host.submitWolfNumber(1, 101));
    ASSERT_TRUE(host.submitWolfNumber(1, 100));
    EXPECT_EQ(host.inputTimeRemaining(1), 0ms);
    EXPECT_FALSE(host.submitWolfNumber(1, 50));
    const auto first = events.round(HostEventType::RoundFinished, 1);
    ASSERT_TRUE(first.result.has_value());
    EXPECT_EQ(first.result->wolfNumber, 100);
    EXPECT_EQ(first.result->caughtCount, 1U);
    EXPECT_FALSE(first.result->gameOver);
    events.round(HostEventType::RoundStarted, 2);
    ASSERT_TRUE(host.submitWolfNumber(2, 100));
    const auto second = events.round(HostEventType::RoundFinished, 2);
    ASSERT_TRUE(second.result.has_value());
    EXPECT_EQ(second.result->consecutiveAllDeadRounds, 2U);
    EXPECT_TRUE(second.result->gameOver);
    events.wait(HostEventType::GameFinished);
    host.join();
    EXPECT_EQ(child.exitCode(), 0);
}

TEST(HostControllerTest, LateClientParticipatesInNextRound) {
    handshake::blockHandshakeSignals();
    Child first(50);
    Child second(50, ChildMode::Normal, true);
    HostController host;
    host.start();
    Events events(host);
    events.round(HostEventType::RoundStarted, 1);
    second.release();
    events.wait(HostEventType::ClientJoined,
                [&](const HostEvent& event) { return event.clientPid == second.pid(); });
    ASSERT_TRUE(host.submitWolfNumber(1, 50));
    const auto one = events.round(HostEventType::RoundFinished, 1);
    ASSERT_TRUE(one.result.has_value());
    EXPECT_EQ(one.result->initialKidCount, 1U);
    EXPECT_EQ(one.result->kids.size(), 1U);
    events.round(HostEventType::RoundStarted, 2);
    ASSERT_TRUE(host.submitWolfNumber(2, 50));
    const auto two = events.round(HostEventType::RoundFinished, 2);
    ASSERT_TRUE(two.result.has_value());
    EXPECT_EQ(two.result->initialKidCount, 2U);
    EXPECT_EQ(two.result->kids.size(), 2U);
    EXPECT_EQ(two.result->hiddenCount, 2U);
    host.requestStop();
    host.join();
    EXPECT_EQ(first.exitCode(), 0);
    EXPECT_EQ(second.exitCode(), 0);
}

TEST(HostControllerTest, SilentClientDoesNotDisconnectHealthyClient) {
    handshake::blockHandshakeSignals();
    Child healthy(50);
    Child silent(50, ChildMode::Silent, true);
    HostController host;
    host.start();
    Events events(host);
    const auto healthyJoined = events.wait(HostEventType::ClientJoined);
    events.round(HostEventType::RoundStarted, 1);
    silent.release();
    const auto silentJoined = events.wait(HostEventType::ClientJoined, [&](const HostEvent& event) {
        return event.clientPid == silent.pid();
    });
    ASSERT_TRUE(host.submitWolfNumber(1, 50));
    events.round(HostEventType::RoundFinished, 1);
    events.round(HostEventType::RoundStarted, 2);
    ASSERT_TRUE(host.submitWolfNumber(2, 50));
    events.wait(HostEventType::ClientLeft,
                [&](const HostEvent& event) { return event.clientId == silentJoined.clientId; });
    const auto result = events.round(HostEventType::RoundFinished, 2);
    ASSERT_TRUE(result.result.has_value());
    EXPECT_EQ(result.result->initialKidCount, 2U);
    ASSERT_EQ(result.result->kids.size(), 1U);
    EXPECT_EQ(result.result->kids.front().clientId, healthyJoined.clientId);
    EXPECT_EQ(result.result->hiddenCount, 1U);
    host.requestStop();
    host.join();
    EXPECT_EQ(healthy.exitCode(), 0);
    EXPECT_EQ(silent.exitCode(), 0);
}

TEST(HostControllerTest, CancelsRoundWhenAllClientsDisconnect) {
    handshake::blockHandshakeSignals();
    Child child(50, ChildMode::Disconnect);
    HostController host;
    host.start();
    Events events(host);
    events.wait(HostEventType::Status);
    const auto cancelled = events.round(HostEventType::RoundFinished, 1);
    ASSERT_TRUE(cancelled.result.has_value());
    EXPECT_FALSE(cancelled.result->completed);
    EXPECT_FALSE(cancelled.result->gameOver);
    EXPECT_EQ(events.wait(HostEventType::Status).text, "Waiting for clients");
    host.requestStop();
    host.join();
    EXPECT_EQ(child.exitCode(), 0);
}

TEST(HostControllerTest, RejectsInvalidNumberFromDeadKid) {
    handshake::blockHandshakeSignals();
    Child child(1, ChildMode::InvalidDeadMove);
    HostController host;
    host.start();
    Events events(host);
    events.round(HostEventType::RoundStarted, 1);
    ASSERT_TRUE(host.submitWolfNumber(1, 100));
    const auto first = events.round(HostEventType::RoundFinished, 1);
    ASSERT_TRUE(first.result.has_value());
    EXPECT_EQ(first.result->deadCount, 1U);
    const auto error = events.wait(HostEventType::Error);
    EXPECT_NE(error.text.find("Invalid move for current kid state"), std::string::npos);
    const auto cancelled = events.round(HostEventType::RoundFinished, 2);
    ASSERT_TRUE(cancelled.result.has_value());
    EXPECT_FALSE(cancelled.result->completed);
    EXPECT_FALSE(cancelled.result->gameOver);
    host.requestStop();
    host.join();
    EXPECT_EQ(child.exitCode(), 0);
}

} // namespace
} // namespace lab2::host
