#pragma once

#include <cstdint>

namespace wolf {

inline constexpr std::int32_t kGoatCount = 1;

enum MessageType : std::int32_t {
    RoundStart = 1,
    GoatThrow = 2,
    RoundResult = 3,
};

struct Message {
    std::int32_t type = 0;
    std::int32_t round = 0;
    std::int32_t value = 0;
    std::int32_t flag = 0;
};

static_assert(sizeof(Message) == 16, "сообщение должно занимать 16 байт");

}
