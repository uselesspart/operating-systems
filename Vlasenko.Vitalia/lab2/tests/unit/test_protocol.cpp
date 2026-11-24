#include "protocol/protocol.hpp"

#include <array>
#include <cerrno>
#include <cstring>
#include <gtest/gtest.h>
#include <limits>
#include <system_error>
#include <vector>

namespace lab2::protocol {
namespace {

constexpr Message kMove{MessageType::ClientMove, 42, 7, 100};

void expectMessage(const Message& actual, const Message& expected) {
    EXPECT_EQ(actual.type, expected.type);
    EXPECT_EQ(actual.clientId, expected.clientId);
    EXPECT_EQ(actual.roundNumber, expected.roundNumber);
    EXPECT_EQ(actual.number, expected.number);
    EXPECT_EQ(actual.state, expected.state);
}

class MemoryConnection final : public ipc::IConnection {
public:
    void read(void* buffer, std::size_t count) override {
        if (count > bytes.size() - readOffset) {
            throw std::system_error(ECONNRESET, std::generic_category(), "Incomplete frame");
        }
        std::memcpy(buffer, bytes.data() + readOffset, count);
        readOffset += count;
    }

    void write(const void* buffer, std::size_t count) override {
        if (failWrite) {
            throw std::system_error(EPIPE, std::generic_category(), "Closed connection");
        }
        const auto* first = static_cast<const std::uint8_t*>(buffer);
        bytes.insert(bytes.end(), first, first + count);
    }

    std::vector<std::uint8_t> bytes;
    std::size_t readOffset{};
    bool failWrite{};
};

TEST(ProtocolTest, EncodesKnownWireBytes) {
    const Message message{MessageType::ClientMove, 0x01020304U, 0x0102030405060708ULL, 100};
    const Frame expected{'L', 'A', 'B', '2', 1, 3, 0, 0, 1, 2,   3, 4,
                         1,   2,   3,   4,   5, 6, 7, 8, 0, 100, 0, 0};
    EXPECT_EQ(encode(message), expected);
    expectMessage(decode(expected), message);
}

TEST(ProtocolTest, RoundTripsAllMessageTypes) {
    const std::array<Message, 8> messages{
        {{MessageType::ClientReady, 42},
         {MessageType::RoundStart, 42, 7},
         kMove,
         {MessageType::RoundResult, 42, 7, 0, WireKidState::Alive},
         {MessageType::RoundResult, 42, 7, 0, WireKidState::Dead},
         {MessageType::GameOver, 42, 7},
         {MessageType::Disconnect, 42},
         {MessageType::Waiting, 42}}};
    for (const auto& message : messages) {
        expectMessage(decode(encode(message)), message);
    }
}

TEST(ProtocolTest, RejectsMalformedHeader) {
    // Сигнатура, версия и тип определяют формат полученного кадра.
    for (const std::size_t offset : {0, 4, 5}) {
        auto frame = encode(kMove);
        frame[offset] = 255;
        EXPECT_THROW(decode(frame), ProtocolError);
    }
}

TEST(ProtocolTest, RejectsOutOfRangeMoves) {
    auto message = kMove;
    for (const std::uint16_t number : {0, 101, 65535}) {
        message.number = number;
        EXPECT_THROW(encode(message), ProtocolError);
    }
    auto frame = encode(kMove);
    frame[20] = 1;
    EXPECT_THROW(decode(frame), ProtocolError);
}

TEST(ProtocolTest, RejectsInvalidResultState) {
    const Message message{MessageType::RoundResult, 42, 7, 0, WireKidState::None};
    EXPECT_THROW(encode(message), ProtocolError);
    auto frame = encode(Message{MessageType::RoundResult, 42, 7, 0, WireKidState::Alive});
    frame[22] = 255;
    EXPECT_THROW(decode(frame), ProtocolError);
}

TEST(ProtocolTest, ReadsConsecutiveFramesIndependently) {
    MemoryConnection connection;
    const Message result{MessageType::RoundResult, 42, 7, 0, WireKidState::Dead};
    sendMessage(connection, kMove);
    sendMessage(connection, result);
    expectMessage(receiveMessage(connection), kMove);
    expectMessage(receiveMessage(connection), result);
    EXPECT_EQ(connection.readOffset, 2 * kFrameSize);
}

TEST(ProtocolTest, PropagatesIncompleteRead) {
    MemoryConnection connection;
    const auto frame = encode(kMove);
    connection.bytes.assign(frame.begin(), frame.end() - 1);
    EXPECT_THROW(receiveMessage(connection), std::system_error);
}

} // namespace
} // namespace lab2::protocol
