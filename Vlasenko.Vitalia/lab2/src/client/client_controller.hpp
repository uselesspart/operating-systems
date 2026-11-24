#pragma once

#include "common/logger.hpp"
#include "game/game_types.hpp"
#include "ipc/connection.hpp"

#include <functional>
#include <sys/types.h>

namespace lab2::client {

class ClientController {
public:
    using MoveGenerator = std::function<int(game::KidState)>;

    explicit ClientController(common::Logger& logger);
    void run(pid_t hostPid);

    // Выделено для проверки последовательности сообщений без реального IPC.
    void play(ipc::IConnection& connection, game::ClientId clientId,
              const MoveGenerator& generateMove);

private:
    common::Logger& logger_;
};

} // namespace lab2::client
