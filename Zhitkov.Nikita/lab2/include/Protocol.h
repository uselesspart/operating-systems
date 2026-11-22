#pragma once
#include <cstdint>

// Тип сообщения в канале. Позволяет хосту отправлять разные сообщения клиенту (число волка в начале раунда, статус в конце раунда), а клиенту - отличать их при чтении
enum MsgType : int32_t {
    WolfNumber = 1,   // host -> client: число волка (начало раунда)
    KidNumber  = 2,   // client -> host: число козлёнка
    Status     = 3,   // host -> client: статус козлёнка после раунда
};

struct HostToClient {
    int32_t type;    // MsgType::WolfNumber или MsgType::Status
    int32_t value;   // wolf_num или kid_alive (1/0)
};

struct ClientToHost {
    int32_t type;    // MsgType::KidNumber
    int32_t value;   // kid_num
};