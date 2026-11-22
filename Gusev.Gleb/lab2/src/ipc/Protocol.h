#pragma once

#include <cstdint>

// Сообщение фиксированного размера: по потоковому каналу всегда идут
// ровно sizeof(Message) байт, поэтому границы сообщений не размываются.
//
//   хост -> козлёнок: ROUND (раунд, жив ли, сколько козлят)
//   козлёнок -> хост: THROW (раунд, число)
//   хост -> козлёнок: RESULT (числа, исход, итоги раунда)
//   вне раунда: GAME_OVER, BYE
namespace wolf::proto {

inline constexpr std::uint32_t kMagic = 0x574F4C46;

enum class Type : std::uint32_t { Round = 1, Throw = 2, Result = 3, GameOver = 4, Bye = 5 };

enum class Outcome : std::uint32_t {
    None = 0,
    Hidden = 1,
    Caught = 2,
    Resurrected = 3,
    StayedDead = 4,
};

struct Message {
    std::uint32_t magic = kMagic;
    Type type = Type::Round;
    std::uint32_t round = 0;
    std::int32_t number = 0;
    std::int32_t wolf = 0;
    std::uint32_t alive = 0;
    Outcome outcome = Outcome::None;
    std::uint32_t kids = 0;
    std::uint32_t hidden = 0;
    std::uint32_t caught = 0;
    std::uint32_t dead = 0;
};

Message makeRound(std::uint32_t round, bool alive, std::uint32_t kids);
Message makeThrow(std::uint32_t round, int number);
Message makeGameOver(std::uint32_t round);
Message makeBye();

bool isValid(const Message& m);

const char* typeName(Type t);
const char* outcomeName(Outcome o);

}
