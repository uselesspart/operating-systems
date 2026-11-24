#pragma once

#include "common/constants.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <sys/types.h>
#include <system_error>

namespace lab2::handshake {

struct HandshakeResult {
    pid_t clientPid;
    std::uint32_t clientId;
    bool newSession;
    std::error_code replyError;
};

// Все методы вызываются одним потоком. Сигналы блокируются до запуска потока.
class HandshakeServer {
public:
    // prepare создаёт ресурсы сессии и возвращает ID (1..INT_MAX) только после их готовности.
    // При исключении prepare отвечает за откат частично созданных ресурсов.
    using PrepareSession = std::function<std::uint32_t(pid_t)>;

    [[nodiscard]] std::optional<HandshakeResult>
    handleNext(const PrepareSession& prepare,
               std::chrono::milliseconds timeout = common::kIpcTimeout);

    // Вызывать при удалении сессии. При EAGAIN можно дождаться повторного запроса;
    // при ESRCH контроллер освобождает ресурсы и удаляет запись.
    void forget(pid_t clientPid);

private:
    std::map<pid_t, std::uint32_t> sessions_;
};

} // namespace lab2::handshake
