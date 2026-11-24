#include "common/deadline.hpp"
#include "common/named_semaphore.hpp"
#include "ipc/conn_sock.hpp"
#include "protocol/protocol.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <future>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
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
    return "lab2-sock-" + std::to_string(geteuid()) + "-" + std::to_string(getpid()) + "-" +
           std::to_string(id);
}

std::string sessionDirectory(std::uint32_t id) {
    return "/tmp/" + sessionName(id);
}

struct ConnectionPair {
    explicit ConnectionPair(std::uint32_t id,
                            std::chrono::milliseconds timeout = common::kIpcTimeout)
        : host(SocketConnection::prepareHost(id, getpid(), timeout)),
          client(SocketConnection::connectClient(getpid(), id, timeout)) {
        host->completeHost();
    }

    std::unique_ptr<SocketConnection> host;
    std::unique_ptr<SocketConnection> client;
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

TEST(SocketConnectionTest, RejectsConnectionFromUnexpectedProcess) {
    constexpr std::uint32_t id = 29;
    // PID 1 не может совпасть с PID тестового клиента в обычной среде CTest.
    auto host = SocketConnection::prepareHost(id, getpid() == 1 ? 2 : 1, 60ms);
    auto unexpected = SocketConnection::connectClient(getpid(), id);
    expectSystemError([&] { host->completeHost(); }, ETIMEDOUT);
    EXPECT_FALSE(host->isConnected());
    struct stat info{};
    EXPECT_EQ(lstat(sessionDirectory(id).c_str(), &info), -1);
    EXPECT_EQ(errno, ENOENT);
}

TEST(SocketConnectionTest, CreatesPrivateSocketAndRemovesOwnedResources) {
    constexpr std::uint32_t id = 12;
    const auto directory = sessionDirectory(id);
    const auto h2c = directory + "/stream.sock";
    const auto semaphore = "/" + sessionName(id) + "-h2c";
    {
        ConnectionPair pair(id);
        struct stat info{};
        ASSERT_EQ(lstat(directory.c_str(), &info), 0);
        EXPECT_TRUE(S_ISDIR(info.st_mode));
        EXPECT_EQ(info.st_mode & 0777, 0700U);
        ASSERT_EQ(lstat(h2c.c_str(), &info), 0);
        EXPECT_TRUE(S_ISSOCK(info.st_mode));
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

TEST(SocketConnectionTest, FailedPreparationRollsBackOnlyOwnedResources) {
    constexpr std::uint32_t id = 26;
    const auto existingName = "/" + sessionName(id) + "-h2c";
    const auto incomingName = "/" + sessionName(id) + "-c2h";
    auto existing = common::NamedSemaphore::create(existingName);
    expectSystemError([] { static_cast<void>(SocketConnection::prepareHost(id, getpid())); },
                      EEXIST);
    struct stat info{};
    EXPECT_EQ(lstat(sessionDirectory(id).c_str(), &info), -1);
    EXPECT_EQ(errno, ENOENT);
    expectSystemError([&] { static_cast<void>(common::NamedSemaphore::open(incomingName)); },
                      ENOENT);
    auto stillOpen = common::NamedSemaphore::open(existingName);
    existing.post();
    EXPECT_TRUE(stillOpen.wait(common::Deadline(60ms)));
}

TEST(SocketConnectionTest, HostCompletionTimesOutWithoutClient) {
    auto host = SocketConnection::prepareHost(15, getpid(), 60ms);
    expectSystemError([&] { host->completeHost(); }, ETIMEDOUT);
    EXPECT_FALSE(host->isConnected());
    struct stat info{};
    EXPECT_EQ(lstat(sessionDirectory(15).c_str(), &info), -1);
    EXPECT_EQ(errno, ENOENT);
}

TEST(SocketConnectionTest, TransfersProtocolFramesInOrder) {
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

TEST(SocketConnectionTest, SeparateSessionsDoNotMixData) {
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

TEST(SocketConnectionTest, ReadTimeoutClosesConnectionAndRemovesHostResources) {
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

TEST(SocketConnectionTest, ClosedPeerWriteReportsErrorInsteadOfSigpipe) {
    ConnectionPair pair(22);
    pair.client.reset();
    const char data = 'x';
    expectSystemError([&] { pair.host->write(&data, 1); }, EPIPE);
    EXPECT_FALSE(pair.host->isConnected());
}

} // namespace
} // namespace lab2::ipc
