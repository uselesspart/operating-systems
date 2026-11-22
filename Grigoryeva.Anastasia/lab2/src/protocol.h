// Протокол обмена между хостом (волк) и клиентами (козлята). Все сообщения фиксированного размера.
#pragma once

#include <cstdint>

enum MsgType : int32_t {
    MSG_HELLO = 1,      // клиент -> хост: "я готов", value = pid клиента
    MSG_REQUEST = 2,    // хост -> клиент: "брось число" (начался ход)
    MSG_NUMBER = 3,     // клиент -> хост: value = число козлёнка
    MSG_STATUS = 4,     // хост -> клиент: value = kStatusAlive / kStatusDead
    MSG_PING = 5,       // хост -> клиент: "я жив" (клиент в ожидании хода)
    MSG_GAME_OVER = 6,  // хост -> клиент: игра окончена
    MSG_BYE = 7         // клиент -> хост: клиент уходит
};

constexpr int32_t kStatusDead = 0;
constexpr int32_t kStatusAlive = 1;

struct Message {
    int32_t type;
    int32_t value;
};
static_assert(sizeof(Message) == 8, "Message must be 8 bytes");

inline Message MakeMessage(int32_t type, int32_t value = 0) { return Message{type, value}; }
