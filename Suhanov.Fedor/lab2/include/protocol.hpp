#pragma once
#include "Connection.hpp"

namespace application {
enum class Kind : std::uint8_t { Hello = 1, Welcome, Chat, Leave, Joined, Left, Failure, Shutdown };
struct Packet {
    Kind kind = Kind::Chat;
    std::uint64_t sequence = 0;
    std::int64_t time_ms = 0;
    int sender = 0;
    int recipient = 0;
    std::string name;
    std::string text;
};
Bytes encode(const Packet& packet);
Packet decode(std::span<const std::uint8_t> bytes);
void validate_name(const std::string& name);
void validate_text(const std::string& text);
std::int64_t now_ms();
} // namespace application
