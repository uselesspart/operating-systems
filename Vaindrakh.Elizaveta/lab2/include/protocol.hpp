#pragma once

#include <cstdint>
#include <stdexcept>

namespace lab {
constexpr int timeoutSeconds = 5;
enum class Kind : std::int32_t { Ready = 1, Roll, Number, Status, Stop, Bye };
struct Message {
    Kind kind = Kind::Ready;
    std::int32_t round = 0;
    std::int32_t value = 0;
    std::int32_t alive = 1;
    std::int32_t finished = 0;
};
inline void expect(const Message& message, Kind kind, int round) {
    if (message.kind != kind || message.round != round)
        throw std::runtime_error("Protocol error: unexpected message or round");
}
}
