#include "common/deadline.hpp"
#include "common/named_semaphore.hpp"
#include "handshake/handshake_client.hpp"
#include "handshake/handshake_server.hpp"
#include "handshake/handshake_signals.hpp"
#include "ipc/conn_fifo.hpp"
#include "protocol/protocol.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <csignal>
#include <cstdlib>
#include <functional>
#include <future>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <sys/wait.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <vector>

namespace lab2::ipc {
namespace {

using namespace std::chrono_literals;
using protocol::MessageType;
using protocol::WireKidState;

std::string sessionName(std::uint32_t id) {
    return "lab2-fifo-" + std::to_string(geteuid()) + "-" + std::to_string(getpid()) + "-" +
           std::to_string(id);
}

std::string sessionDirectory(std::uint32_t id) {
    return "/tmp/" + sessionName(id);
}

void prepareSignals() {
    handshake::blockHandshakeSignals();
    sigset_t signals{};
    sigemptyset(&signals);
    sigaddset(&signals, SIGUSR1);
    sigaddset(&signals, SIGUSR2);
    const timespec immediate{};
    while (sigtimedwait(&signals, nullptr, &immediate) >= 0) {
    }
}

struct ConnectionPair {
    explicit ConnectionPair(std::uint32_t id,
                            std::chrono::milliseconds timeout = common::kIpcTimeout)
        : host(FifoConnection::prepareHost(id, timeout)),
          client(FifoConnection::connectClient(getpid(), id, timeout)) {
        host->completeHost();
    }

    std::unique_ptr<FifoConnection> host;
    std::unique_ptr<FifoConnection> client;
};

template <typename Operation> void expectSystemError(Operation operation, int expectedError) {
    try {
        operation();
    } catch (const std::system_error& error) {
        EXPECT_EQ(error.code().value(), expectedError);
        return;
    }
    ADD_FAILURE() << "Expected system error " << expectedError;
}

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

    [[nodiscard]] pid_t pid() const noexcept {
        return pid_;
    }

    bool succeeded() {
        const common::Deadline deadline(5s);
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

TEST(FifoConnectionTest, CreatesPrivateFifosAndRemovesOwnedResources) {
    constexpr std::uint32_t id = 12;
    const auto directory = sessionDirectory(id);
    const auto h2c = directory + "/h2c";
    const auto c2h = directory + "/c2h";
    const auto semaphore = "/" + sessionName(id) + "-h2c";
    {
        ConnectionPair pair(id);
        struct stat info{};
        ASSERT_EQ(lstat(directory.c_str(), &info), 0);
        EXPECT_TRUE(S_ISDIR(info.st_mode));
        EXPECT_EQ(info.st_mode & 0777, 0700U);
        ASSERT_EQ(lstat(h2c.c_str(), &info), 0);
        EXPECT_TRUE(S_ISFIFO(info.st_mode));
        EXPECT_EQ(info.st_mode & 0777, 0600U);
        ASSERT_EQ(lstat(c2h.c_str(), &info), 0);
        EXPECT_TRUE(S_ISFIFO(info.st_mode));
        EXPECT_EQ(info.st_mode & 0777, 0600U);
        EXPECT_NO_THROW(static_cast<void>(common::NamedSemaphore::open(semaphore)));

        pair.client.reset();
        EXPECT_EQ(lstat(h2c.c_str(), &info), 0);
    }
    struct stat info{};
    EXPECT_EQ(lstat(directory.c_str(), &info), -1);
    EXPECT_EQ(errno, ENOENT);
    expectSystemError([&] { static_cast<void>(common::NamedSemaphore::open(semaphore)); }, ENOENT);
}

TEST(FifoConnectionTest, FailedPreparationRollsBackOnlyOwnedResources) {
    constexpr std::uint32_t id = 26;
    const auto existingName = "/" + sessionName(id) + "-h2c";
    const auto incomingName = "/" + sessionName(id) + "-c2h";
    auto existing = common::NamedSemaphore::create(existingName);
    expectSystemError([] { static_cast<void>(FifoConnection::prepareHost(id)); }, EEXIST);
    struct stat info{};
    EXPECT_EQ(lstat(sessionDirectory(id).c_str(), &info), -1);
    EXPECT_EQ(errno, ENOENT);
    expectSystemError([&] { static_cast<void>(common::NamedSemaphore::open(incomingName)); },
                      ENOENT);
    auto stillOpen = common::NamedSemaphore::open(existingName);
    existing.post();
    EXPECT_TRUE(stillOpen.wait(common::Deadline(60ms)));
}

TEST(FifoConnectionTest, HostCompletionTimesOutWithoutClient) {
    auto host = FifoConnection::prepareHost(15, 60ms);
    expectSystemError([&] { host->completeHost(); }, ETIMEDOUT);
    EXPECT_FALSE(host->isConnected());
    struct stat info{};
    EXPECT_EQ(lstat(sessionDirectory(15).c_str(), &info), -1);
    EXPECT_EQ(errno, ENOENT);
}

TEST(FifoConnectionTest, TransfersProtocolFramesInOrder) {
    ConnectionPair pair(17);
    protocol::sendMessage(*pair.client, {MessageType::ClientReady, 17});
    protocol::sendMessage(*pair.client, {MessageType::ClientMove, 17, 1, 42});
    EXPECT_EQ(protocol::receiveMessage(*pair.host).type, MessageType::ClientReady);
    const auto move = protocol::receiveMessage(*pair.host);
    EXPECT_EQ(move.type, MessageType::ClientMove);
    EXPECT_EQ(move.number, 42);
    EXPECT_EQ(move.roundNumber, 1U);
    protocol::sendMessage(*pair.host, {MessageType::RoundResult, 17, 1, 0, WireKidState::Alive});
    EXPECT_EQ(protocol::receiveMessage(*pair.client).state, WireKidState::Alive);
}

TEST(FifoConnectionTest, SeparateSessionsDoNotMixData) {
    ConnectionPair first(18);
    ConnectionPair second(19);
    const char a = 'a';
    const char b = 'b';
    char received{};
    first.client->write(&a, 1);
    second.client->write(&b, 1);
    second.host->read(&received, 1);
    EXPECT_EQ(received, b);
    first.host->read(&received, 1);
    EXPECT_EQ(received, a);
}

TEST(FifoConnectionTest, ReadTimeoutClosesConnectionAndRemovesHostResources) {
    ConnectionPair pair(21, 60ms);
    char data{};
    expectSystemError([&] { pair.host->read(&data, 1); }, ETIMEDOUT);
    EXPECT_FALSE(pair.host->isConnected());
    EXPECT_THROW(pair.host->read(&data, 1), std::logic_error);
    struct stat info{};
    EXPECT_EQ(lstat(sessionDirectory(21).c_str(), &info), -1);
    EXPECT_EQ(errno, ENOENT);
    expectSystemError([&] { pair.client->write(&data, 1); }, EPIPE);
}

TEST(FifoConnectionTest, ClosedPeerWriteReportsErrorInsteadOfSigpipe) {
    ConnectionPair pair(22);
    pair.client.reset();
    const char data = 'x';
    expectSystemError([&] { pair.host->write(&data, 1); }, EPIPE);
    EXPECT_FALSE(pair.host->isConnected());
}

TEST(FifoConnectionTest, HandshakeAndProtocolAcrossProcesses) {
    prepareSignals();
    const pid_t hostPid = getpid();
    constexpr std::uint32_t id = 25;

    // Хост создаёт FIFO уже после fork: клиент не наследует дескрипторы каналов.
    Child child([hostPid] {
        const auto clientId = handshake::HandshakeClient{}.connect(hostPid);
        if (clientId != id) {
            return false;
        }
        auto client = FifoConnection::connectClient(hostPid, clientId);
        protocol::sendMessage(*client, {MessageType::ClientReady, clientId});
        const auto start = protocol::receiveMessage(*client);
        if (start.type != MessageType::RoundStart || start.roundNumber != 1) {
            return false;
        }
        protocol::sendMessage(*client, {MessageType::ClientMove, clientId, 1, 42});
        const auto result = protocol::receiveMessage(*client);
        return result.type == MessageType::RoundResult && result.state == WireKidState::Alive;
    });

    handshake::HandshakeServer server;
    std::unique_ptr<FifoConnection> host;
    const auto request = server.handleNext([&](pid_t pid) -> std::uint32_t {
        if (pid != child.pid()) {
            throw std::logic_error("Unexpected client PID");
        }
        host = FifoConnection::prepareHost(id);
        return id;
    });
    ASSERT_TRUE(request.has_value());
    ASSERT_TRUE(request->newSession);
    ASSERT_FALSE(request->replyError);
    ASSERT_TRUE(host);
    host->completeHost();
    EXPECT_EQ(protocol::receiveMessage(*host).type, MessageType::ClientReady);
    protocol::sendMessage(*host, {MessageType::RoundStart, id, 1});
    const auto move = protocol::receiveMessage(*host);
    EXPECT_EQ(move.type, MessageType::ClientMove);
    EXPECT_EQ(move.number, 42);
    protocol::sendMessage(*host, {MessageType::RoundResult, id, 1, 0, WireKidState::Alive});
    EXPECT_TRUE(child.succeeded());
}

} // namespace
} // namespace lab2::ipc
