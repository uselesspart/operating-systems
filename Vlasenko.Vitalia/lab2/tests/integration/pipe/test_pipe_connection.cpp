#include "common/deadline.hpp"
#include "common/named_semaphore.hpp"
#include "common/unique_fd.hpp"
#include "ipc/conn_pipe.hpp"
#include "protocol/protocol.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <future>
#include <gtest/gtest.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
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
    return "lab2-pipe-" + std::to_string(geteuid()) + "-" + std::to_string(getpid()) + "-" +
           std::to_string(id);
}

std::string sessionDirectory(std::uint32_t id) {
    return "/tmp/" + sessionName(id);
}

struct ConnectionPair {
    explicit ConnectionPair(std::uint32_t id,
                            std::chrono::milliseconds timeout = common::kIpcTimeout)
        : host(PipeConnection::prepareHost(id, getpid(), timeout)) {
        auto pending = std::async(std::launch::async, [&] {
            return PipeConnection::connectClient(getpid(), id, timeout);
        });
        host->completeHost();
        client = pending.get();
    }

    std::unique_ptr<PipeConnection> host;
    std::unique_ptr<PipeConnection> client;
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

TEST(PipeConnectionTest, RejectsConnectionFromUnexpectedProcess) {
    constexpr std::uint32_t id = 29;
    auto host = PipeConnection::prepareHost(id, getpid() == 1 ? 2 : 1, 60ms);
    common::UniqueFd unexpected(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
    ASSERT_TRUE(unexpected);
    sockaddr_un endpoint{};
    endpoint.sun_family = AF_UNIX;
    const auto path = sessionDirectory(id) + "/transfer.sock";
    ASSERT_TRUE(path.size() < sizeof(endpoint.sun_path));
    std::memcpy(endpoint.sun_path, path.c_str(), path.size() + 1);
    ASSERT_EQ(
        connect(unexpected.get(), reinterpret_cast<const sockaddr*>(&endpoint), sizeof(endpoint)),
        0);
    expectSystemError([&] { host->completeHost(); }, ETIMEDOUT);
    EXPECT_FALSE(host->isConnected());
    char marker{};
    EXPECT_EQ(recv(unexpected.get(), &marker, 1, MSG_DONTWAIT), 0);
    struct stat info{};
    EXPECT_EQ(lstat(sessionDirectory(id).c_str(), &info), -1);
    EXPECT_EQ(errno, ENOENT);
}

TEST(PipeConnectionTest, CreatesPrivateEndpointAndRemovesOwnedResources) {
    constexpr std::uint32_t id = 12;
    const auto directory = sessionDirectory(id);
    const auto h2c = directory + "/transfer.sock";
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

TEST(PipeConnectionTest, FailedPreparationRollsBackOnlyOwnedResources) {
    constexpr std::uint32_t id = 26;
    const auto existingName = "/" + sessionName(id) + "-h2c";
    const auto incomingName = "/" + sessionName(id) + "-c2h";
    auto existing = common::NamedSemaphore::create(existingName);
    expectSystemError([] { static_cast<void>(PipeConnection::prepareHost(id, getpid())); }, EEXIST);
    struct stat info{};
    EXPECT_EQ(lstat(sessionDirectory(id).c_str(), &info), -1);
    EXPECT_EQ(errno, ENOENT);
    expectSystemError([&] { static_cast<void>(common::NamedSemaphore::open(incomingName)); },
                      ENOENT);
    auto stillOpen = common::NamedSemaphore::open(existingName);
    existing.post();
    EXPECT_TRUE(stillOpen.wait(common::Deadline(60ms)));
}

TEST(PipeConnectionTest, TransfersProtocolFramesInOrder) {
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

TEST(PipeConnectionTest, SeparateSessionsDoNotMixData) {
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

TEST(PipeConnectionTest, ReadTimeoutClosesConnectionAndRemovesHostResources) {
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

TEST(PipeConnectionTest, ClosedPeerWriteReportsErrorInsteadOfSigpipe) {
    ConnectionPair pair(22);
    pair.client.reset();
    const char data = 'x';
    expectSystemError([&] { pair.host->write(&data, 1); }, EPIPE);
    EXPECT_FALSE(pair.host->isConnected());
}

TEST(PipeConnectionTest, ReceivesRealPipeEndsAndClosesAuxiliarySocket) {
    constexpr std::uint32_t id = 30;
    auto host = PipeConnection::prepareHost(id, getpid());
    common::UniqueFd socketFd(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0));
    ASSERT_TRUE(socketFd);
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const auto path = sessionDirectory(id) + "/transfer.sock";
    ASSERT_TRUE(path.size() < sizeof(address.sun_path));
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    ASSERT_EQ(connect(socketFd.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)),
              0);
    host->completeHost();

    unsigned char marker{};
    iovec payload{&marker, sizeof(marker)};
    alignas(cmsghdr) unsigned char control[CMSG_SPACE(2 * sizeof(int))]{};
    msghdr message{};
    message.msg_iov = &payload;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    ASSERT_EQ(recvmsg(socketFd.get(), &message, MSG_CMSG_CLOEXEC), 1);
    auto* header = CMSG_FIRSTHDR(&message);
    ASSERT_TRUE(header != nullptr);
    ASSERT_EQ(header->cmsg_level, SOL_SOCKET);
    ASSERT_EQ(header->cmsg_type, SCM_RIGHTS);
    ASSERT_EQ(header->cmsg_len, CMSG_LEN(2 * sizeof(int)));
    int descriptors[2]{};
    std::memcpy(descriptors, CMSG_DATA(header), sizeof(descriptors));
    common::UniqueFd reader(descriptors[0]);
    common::UniqueFd writer(descriptors[1]);
    EXPECT_EQ(marker, 1);
    struct stat readInfo{}, writeInfo{};
    ASSERT_EQ(fstat(reader.get(), &readInfo), 0);
    ASSERT_EQ(fstat(writer.get(), &writeInfo), 0);
    EXPECT_TRUE(S_ISFIFO(readInfo.st_mode));
    EXPECT_TRUE(S_ISFIFO(writeInfo.st_mode));
    EXPECT_NE(readInfo.st_ino, writeInfo.st_ino);
    EXPECT_EQ(fcntl(reader.get(), F_GETFL) & O_ACCMODE, O_RDONLY);
    EXPECT_EQ(fcntl(writer.get(), F_GETFL) & O_ACCMODE, O_WRONLY);
    EXPECT_TRUE((fcntl(reader.get(), F_GETFD) & FD_CLOEXEC) != 0);
    EXPECT_TRUE((fcntl(writer.get(), F_GETFD) & FD_CLOEXEC) != 0);
    // Хост закрыл вспомогательный сокет, но оба канала продолжают работать.
    EXPECT_EQ(recv(socketFd.get(), &marker, 1, MSG_DONTWAIT), 0);
    socketFd.reset();
    auto incoming = common::NamedSemaphore::open("/" + sessionName(id) + "-h2c");
    auto outgoing = common::NamedSemaphore::open("/" + sessionName(id) + "-c2h");
    const char request = 'x';
    char response{};
    host->write(&request, 1);
    ASSERT_TRUE(incoming.wait(common::Deadline(500ms)));
    ASSERT_EQ(::read(reader.get(), &response, 1), 1);
    EXPECT_EQ(response, request);
    ASSERT_EQ(::write(writer.get(), &request, 1), 1);
    outgoing.post();
    host->read(&response, 1);
    EXPECT_EQ(response, request);
}

TEST(PipeConnectionTest, ClientTimesOutWaitingForDescriptors) {
    constexpr std::uint32_t id = 31;
    auto host = PipeConnection::prepareHost(id, getpid());
    expectSystemError([&] { static_cast<void>(PipeConnection::connectClient(getpid(), id, 60ms)); },
                      ETIMEDOUT);
    EXPECT_FALSE(host->isConnected());
}

// Имитируем хост только на этапе передачи fd, чтобы проверить реакцию
// настоящего клиента на повреждённый пакет и количество открытых дескрипторов.
class InvalidTransferServer {
public:
    explicit InvalidTransferServer(std::uint32_t id)
        : directory_(sessionDirectory(id)), path_(directory_ + "/transfer.sock"),
          incoming_(common::NamedSemaphore::create("/" + sessionName(id) + "-c2h")),
          outgoing_(common::NamedSemaphore::create("/" + sessionName(id) + "-h2c")) {
        if (mkdir(directory_.c_str(), 0700) == -1) {
            throw std::system_error(errno, std::generic_category(), "mkdir(test endpoint)");
        }
        try {
            listener_.reset(socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
            if (!listener_) {
                throw std::system_error(errno, std::generic_category(), "socket(test endpoint)");
            }
            sockaddr_un endpoint{};
            endpoint.sun_family = AF_UNIX;
            if (path_.size() >= sizeof(endpoint.sun_path)) {
                throw std::logic_error("Test endpoint path too long");
            }
            std::memcpy(endpoint.sun_path, path_.c_str(), path_.size() + 1);
            if (bind(listener_.get(), reinterpret_cast<const sockaddr*>(&endpoint),
                     sizeof(endpoint)) == -1 ||
                chmod(path_.c_str(), 0600) == -1 || listen(listener_.get(), 1) == -1) {
                throw std::system_error(errno, std::generic_category(), "prepare(test endpoint)");
            }
        } catch (...) {
            cleanup();
            throw;
        }
    }
    ~InvalidTransferServer() {
        cleanup();
    }
    InvalidTransferServer(const InvalidTransferServer&) = delete;
    InvalidTransferServer& operator=(const InvalidTransferServer&) = delete;

    void send(const std::vector<int>& descriptors, unsigned char marker = 1) {
        const common::Deadline deadline(1s);
        common::UniqueFd peer;
        while (!peer) {
            peer.reset(accept4(listener_.get(), nullptr, nullptr, SOCK_CLOEXEC));
            if (peer) {
                break;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                throw std::system_error(errno, std::generic_category(), "accept4(test endpoint)");
            }
            if (deadline.remainingMilliseconds() == 0) {
                throw std::runtime_error("Test client did not connect");
            }
            std::this_thread::sleep_for(1ms);
        }
        iovec payload{&marker, sizeof(marker)};
        alignas(cmsghdr) unsigned char control[CMSG_SPACE(12 * sizeof(int))]{};
        msghdr message{};
        message.msg_iov = &payload;
        message.msg_iovlen = 1;
        if (!descriptors.empty()) {
            if (descriptors.size() > 12) {
                throw std::invalid_argument("Too many test descriptors");
            }
            const auto bytes = descriptors.size() * sizeof(int);
            message.msg_control = control;
            message.msg_controllen = CMSG_SPACE(bytes);
            auto* header = CMSG_FIRSTHDR(&message);
            header->cmsg_level = SOL_SOCKET;
            header->cmsg_type = SCM_RIGHTS;
            header->cmsg_len = CMSG_LEN(bytes);
            std::memcpy(CMSG_DATA(header), descriptors.data(), bytes);
        }
        if (sendmsg(peer.get(), &message, MSG_NOSIGNAL) != 1) {
            throw std::system_error(errno, std::generic_category(), "sendmsg(test endpoint)");
        }
    }

private:
    void cleanup() noexcept {
        listener_.reset();
        static_cast<void>(unlink(path_.c_str()));
        static_cast<void>(rmdir(directory_.c_str()));
    }
    std::string directory_;
    std::string path_;
    common::NamedSemaphore incoming_;
    common::NamedSemaphore outgoing_;
    common::UniqueFd listener_;
};

std::size_t openDescriptorCount() {
    std::size_t count{};
    for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd")) {
        static_cast<void>(entry);
        ++count;
    }
    return count;
}

void expectInvalidTransfer(std::uint32_t id, const std::vector<int>& descriptors,
                           unsigned char marker = 1) {
    InvalidTransferServer server(id);
    const auto before = openDescriptorCount();
    auto pending = std::async(std::launch::async, [&] {
        try {
            static_cast<void>(PipeConnection::connectClient(getpid(), id, 2s));
            return 0;
        } catch (const std::system_error& error) {
            return error.code().value();
        }
    });
    server.send(descriptors, marker);
    EXPECT_EQ(pending.get(), EPROTO);
    EXPECT_EQ(openDescriptorCount(), before);
}

TEST(PipeConnectionTest, RejectsMissingOrExtraDescriptorsWithoutLeaks) {
    common::UniqueFd fd(open("/dev/null", O_RDONLY | O_CLOEXEC));
    ASSERT_TRUE(fd);
    expectInvalidTransfer(32, {});
    expectInvalidTransfer(33, {fd.get()});
    expectInvalidTransfer(34, {fd.get(), fd.get(), fd.get()});
    // Управляющее сообщение больше буфера клиента: ядро закроет лишние fd,
    // а приложение обязано закрыть все фактически полученные.
    expectInvalidTransfer(35, std::vector<int>(12, fd.get()));
}

TEST(PipeConnectionTest, RejectsNonPipeDescriptorsWithoutLeaks) {
    common::UniqueFd reader(open("/dev/null", O_RDONLY | O_NONBLOCK | O_CLOEXEC));
    common::UniqueFd writer(open("/dev/null", O_WRONLY | O_NONBLOCK | O_CLOEXEC));
    ASSERT_TRUE(reader);
    ASSERT_TRUE(writer);
    expectInvalidTransfer(36, {reader.get(), writer.get()});
}

TEST(PipeConnectionTest, RejectsWrongDirectionsAndInvalidMarkerWithoutLeaks) {
    int descriptors[2]{};
    ASSERT_EQ(pipe2(descriptors, O_NONBLOCK | O_CLOEXEC), 0);
    common::UniqueFd reader(descriptors[0]);
    common::UniqueFd writer(descriptors[1]);
    expectInvalidTransfer(37, {writer.get(), reader.get()});
    expectInvalidTransfer(38, {reader.get(), writer.get()}, 2);
}

} // namespace
} // namespace lab2::ipc
