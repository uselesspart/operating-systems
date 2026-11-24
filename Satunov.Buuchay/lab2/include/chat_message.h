#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace chat {

    inline constexpr std::size_t kMaxTextBytes = 1024;
    inline constexpr std::size_t kHeaderBytes = 18;

    enum class MessageKind : std::uint8_t {
        Broadcast = 1,
        Private = 2
    };

    enum class Participant : std::uint8_t {
        All = 0,
        Host = 1,
        Client = 2
    };

    struct ChatMessage {
        MessageKind kind;
        Participant sender;
        Participant recipient;
        std::uint64_t sent_at_ms;
        std::string text;

        bool operator==(const ChatMessage&) const = default;
    };

    [[nodiscard]] std::optional<std::vector<std::uint8_t>>
    serialize(const ChatMessage& message);

    [[nodiscard]] std::optional<ChatMessage>
    deserialize(std::span<const std::uint8_t> bytes);

} // namespace chat