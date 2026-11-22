#include "ipc/Protocol.h"

namespace wolf::proto {

Message makeRound(std::uint32_t round, bool alive, std::uint32_t kids) {
    Message m;
    m.type = Type::Round;
    m.round = round;
    m.alive = alive ? 1 : 0;
    m.kids = kids;
    return m;
}

Message makeThrow(std::uint32_t round, int number) {
    Message m;
    m.type = Type::Throw;
    m.round = round;
    m.number = number;
    return m;
}

Message makeGameOver(std::uint32_t round) {
    Message m;
    m.type = Type::GameOver;
    m.round = round;
    return m;
}

Message makeBye() {
    Message m;
    m.type = Type::Bye;
    return m;
}

bool isValid(const Message& m) {
    if (m.magic != kMagic) return false;
    switch (m.type) {
        case Type::Round:
        case Type::Throw:
        case Type::Result:
        case Type::GameOver:
        case Type::Bye:
            return true;
    }
    return false;
}

const char* typeName(Type t) {
    switch (t) {
        case Type::Round:    return "ROUND";
        case Type::Throw:    return "THROW";
        case Type::Result:   return "RESULT";
        case Type::GameOver: return "GAME_OVER";
        case Type::Bye:      return "BYE";
    }
    return "?";
}

const char* outcomeName(Outcome o) {
    switch (o) {
        case Outcome::None:        return "-";
        case Outcome::Hidden:      return "спрятался";
        case Outcome::Caught:      return "попался";
        case Outcome::Resurrected: return "воскрес";
        case Outcome::StayedDead:  return "остался мёртв";
    }
    return "?";
}

}
