#pragma once
#include <cstdint>

namespace proto {

constexpr int kWolfInputTimeoutMs = 3000;
constexpr int kKidMaxAlive = 100;
constexpr int kKidMaxDead = 50;
constexpr int kHideRange = 70;
constexpr int kReviveRange = 20;
constexpr int kMaxDeadRounds = 2;

enum MsgType : int32_t {
    kJoin = 1,
    kNumber = 2,
    kStatus = 3,
    kGameOver = 4
};

struct Msg {
    int32_t type;
    int32_t value;
};

}
