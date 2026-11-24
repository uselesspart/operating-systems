#include "protocol/protocol.hpp"

#include <algorithm>
#include <array>

namespace lab2::protocol {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'L', 'A', 'B', '2'};
constexpr std::size_t kVersionOffset = 4;
constexpr std::size_t kTypeOffset = 5;
constexpr std::size_t kClientIdOffset = 8;
constexpr std::size_t kRoundOffset = 12;
constexpr std::size_t kNumberOffset = 20;
constexpr std::size_t kStateOffset = 22;
constexpr std::uint16_t kMaxMove = 100;

bool isKidState(WireKidState state) noexcept {
    return state == WireKidState::Alive || state == WireKidState::Dead;
}

void validate(const Message& message) {
    if (message.clientId == 0) {
        throw ProtocolError("Invalid client ID");
    }

    const bool hasRound = message.roundNumber != 0;
    const bool hasNumber = message.number != 0;
    const bool hasState = message.state != WireKidState::None;

    switch (message.type) {
    case MessageType::ClientReady:
    case MessageType::Disconnect:
    case MessageType::Waiting:
        if (hasRound || hasNumber || hasState) {
            throw ProtocolError("Unexpected fields in control message");
        }
        break;
    case MessageType::RoundStart:
    case MessageType::GameOver:
        if (!hasRound || hasNumber || hasState) {
            throw ProtocolError("Invalid round control message");
        }
        break;
    case MessageType::ClientMove:
        if (!hasRound || !hasNumber || message.number > kMaxMove || hasState) {
            throw ProtocolError("Invalid client move");
        }
        break;
    case MessageType::RoundResult:
        if (!hasRound || hasNumber || !isKidState(message.state)) {
            throw ProtocolError("Invalid round result");
        }
        break;
    default:
        throw ProtocolError("Unknown message type");
    }
}

// Сетевой порядок байтов задаётся явно, без зависимости от padding и ABI.
void writeInteger(Frame& frame, std::size_t offset, std::size_t width, std::uint64_t value) {
    for (std::size_t i = width; i > 0; --i) {
        frame[offset + i - 1] = static_cast<std::uint8_t>(value & 0xffU);
        value >>= 8U;
    }
}

std::uint64_t readInteger(const Frame& frame, std::size_t offset, std::size_t width) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < width; ++i) {
        value = (value << 8U) | frame[offset + i];
    }
    return value;
}

} // namespace

Frame encode(const Message& message) {
    validate(message);

    Frame frame{};
    std::copy(kMagic.begin(), kMagic.end(), frame.begin());
    frame[kVersionOffset] = kProtocolVersion;
    frame[kTypeOffset] = static_cast<std::uint8_t>(message.type);
    writeInteger(frame, kClientIdOffset, 4, message.clientId);
    writeInteger(frame, kRoundOffset, 8, message.roundNumber);
    writeInteger(frame, kNumberOffset, 2, message.number);
    frame[kStateOffset] = static_cast<std::uint8_t>(message.state);
    return frame;
}

Message decode(const Frame& frame) {
    if (!std::equal(kMagic.begin(), kMagic.end(), frame.begin())) {
        throw ProtocolError("Invalid protocol magic");
    }
    if (frame[kVersionOffset] != kProtocolVersion) {
        throw ProtocolError("Unsupported protocol version");
    }
    if (frame[6] != 0 || frame[7] != 0 || frame[23] != 0) {
        throw ProtocolError("Nonzero reserved bytes");
    }

    const Message message{static_cast<MessageType>(frame[kTypeOffset]),
                          static_cast<std::uint32_t>(readInteger(frame, kClientIdOffset, 4)),
                          readInteger(frame, kRoundOffset, 8),
                          static_cast<std::uint16_t>(readInteger(frame, kNumberOffset, 2)),
                          static_cast<WireKidState>(frame[kStateOffset])};
    validate(message);
    return message;
}

void sendMessage(ipc::IConnection& connection, const Message& message) {
    const auto frame = encode(message);
    connection.write(frame.data(), frame.size());
}

Message receiveMessage(ipc::IConnection& connection) {
    Frame frame{};
    // IConnection гарантирует передачу всего кадра или исключение.
    // После ошибки чтения границы кадров восстановить нельзя.
    connection.read(frame.data(), frame.size());
    return decode(frame);
}

} // namespace lab2::protocol
