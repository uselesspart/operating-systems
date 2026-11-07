#pragma once

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <type_traits>

enum class Party : std::uint8_t { Host, Client, Everyone };
enum class Kind : std::uint8_t { Text, Bye };

constexpr std::size_t kMaxTextBytes = 1024;

struct WireMessage {
    Kind kind = Kind::Text;
    Party sender = Party::Host;
    Party recipient = Party::Everyone;
    std::uint32_t length = 0;
    std::int64_t sentAtUs = 0;
    char text[kMaxTextBytes] = {};
};

static_assert(std::is_trivially_copyable_v<WireMessage>);
static_assert(sizeof(WireMessage) <= 4096, "кадр должен помещаться в PIPE_BUF для атомарной записи в FIFO");

constexpr Party peerOf(Party party)
{
    return party == Party::Host ? Party::Client : Party::Host;
}

inline std::int64_t nowMicros()
{
    using namespace std::chrono;
    return duration_cast<microseconds>(system_clock::now().time_since_epoch()).count();
}

inline std::optional<WireMessage> makeText(Party sender, Party recipient, std::string_view text)
{
    if (text.size() > kMaxTextBytes)
        return std::nullopt;
    WireMessage message;
    message.sender = sender;
    message.recipient = recipient;
    message.sentAtUs = nowMicros();
    message.length = static_cast<std::uint32_t>(text.size());
    std::copy(text.begin(), text.end(), message.text);
    return message;
}

inline WireMessage makeBye(Party sender)
{
    WireMessage message;
    message.kind = Kind::Bye;
    message.sender = sender;
    message.sentAtUs = nowMicros();
    return message;
}

inline std::string_view textOf(const WireMessage& message)
{
    return {message.text, message.length};
}

inline bool isValid(const WireMessage& message)
{
    return message.kind <= Kind::Bye && message.sender < Party::Everyone && message.recipient <= Party::Everyone
           && message.length <= kMaxTextBytes;
}

inline bool isVisibleTo(const WireMessage& message, Party viewer)
{
    return message.recipient == Party::Everyone || message.sender == viewer || message.recipient == viewer;
}
