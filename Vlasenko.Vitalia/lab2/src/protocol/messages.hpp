#pragma once

#include <cstdint>

namespace lab2::protocol {

enum class MessageType : std::uint8_t {
    ClientReady = 1,
    RoundStart = 2,
    ClientMove = 3,
    RoundResult = 4,
    GameOver = 5,
    Disconnect = 6,
    Waiting = 7
};

// Значения протокола не зависят от представления enum в игровой логике.
enum class WireKidState : std::uint8_t { None = 0, Alive = 1, Dead = 2 };

struct Message {
    MessageType type{};
    std::uint32_t clientId{};
    std::uint64_t roundNumber{};
    std::uint16_t number{};
    WireKidState state{WireKidState::None};
};

} // namespace lab2::protocol
