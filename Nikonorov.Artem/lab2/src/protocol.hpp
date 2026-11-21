#pragma once

#include <cstdint>

constexpr int kGoatCount = 1;

constexpr std::int32_t kMessageRound = 1;
constexpr std::int32_t kMessageGoat = 2;
constexpr std::int32_t kMessageResult = 3;

struct Message {
    std::int32_t type;
    std::int32_t round;
    std::int32_t value;
    std::int32_t flag;
};

static_assert(sizeof(Message) == 16, "Message must have a stable 16-byte layout");
