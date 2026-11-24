#include "chat_message.h"

#include <algorithm>
#include <array>

namespace chat {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'C', 'H', 'A', 'T'};
constexpr std::uint8_t kVersion = 1;

bool valid(const ChatMessage& message) {
    if (message.sender != Participant::Host &&
        message.sender != Participant::Client) {
        return false;
    }

    if (message.text.empty() ||
        message.text.size() > kMaxTextBytes) {
        return false;
    }

    if (message.kind == MessageKind::Broadcast) {
        return message.recipient == Participant::All;
    }

    if (message.kind == MessageKind::Private) {
        return (message.recipient == Participant::Host ||
                message.recipient == Participant::Client) &&
               message.recipient != message.sender;
    }

    return false;
}

void append_u16(std::vector<std::uint8_t>& out,
                std::uint16_t number) {
    out.push_back(static_cast<std::uint8_t>(number >> 8));
    out.push_back(static_cast<std::uint8_t>(number));
}

void append_u64(std::vector<std::uint8_t>& out,
                std::uint64_t number) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        out.push_back(static_cast<std::uint8_t>(number >> shift));
    }
}

std::uint16_t read_u16(std::span<const std::uint8_t> data,
                       std::size_t at) {
    return (static_cast<std::uint16_t>(data[at]) << 8) |
           data[at + 1];
}

std::uint64_t read_u64(std::span<const std::uint8_t> data,
                       std::size_t at) {
    std::uint64_t number = 0;

    for (std::size_t i = 0; i < 8; ++i) {
        number = (number << 8) | data[at + i];
    }

    return number;
}

} // namespace

std::optional<std::vector<std::uint8_t>>
serialize(const ChatMessage& message) {
    if (!valid(message)) {
        return std::nullopt;
    }

    std::vector<std::uint8_t> bytes;
    bytes.reserve(kHeaderBytes + message.text.size());

    bytes.insert(bytes.end(), kMagic.begin(), kMagic.end());
    bytes.push_back(kVersion);
    bytes.push_back(static_cast<std::uint8_t>(message.kind));
    bytes.push_back(static_cast<std::uint8_t>(message.sender));
    bytes.push_back(static_cast<std::uint8_t>(message.recipient));

    append_u64(bytes, message.sent_at_ms);
    append_u16(bytes, static_cast<std::uint16_t>(message.text.size()));

    bytes.insert(bytes.end(), message.text.begin(), message.text.end());

    return bytes;
}

std::optional<ChatMessage>
deserialize(std::span<const std::uint8_t> bytes) {
    if (bytes.size() < kHeaderBytes ||
        !std::equal(kMagic.begin(), kMagic.end(), bytes.begin()) ||
        bytes[4] != kVersion) {
        return std::nullopt;
    }

    const auto size = read_u16(bytes, 16);

    if (size == 0 ||
        size > kMaxTextBytes ||
        bytes.size() != kHeaderBytes + size) {
        return std::nullopt;
    }

    ChatMessage message{
        static_cast<MessageKind>(bytes[5]),
        static_cast<Participant>(bytes[6]),
        static_cast<Participant>(bytes[7]),
        read_u64(bytes, 8),
        std::string(bytes.begin() + kHeaderBytes, bytes.end())
    };

    if (!valid(message)) {
        return std::nullopt;
    }

    return message;
}

} // namespace chat