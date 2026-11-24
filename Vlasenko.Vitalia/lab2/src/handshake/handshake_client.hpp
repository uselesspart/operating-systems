#pragma once

#include "common/constants.hpp"

#include <cstdint>
#include <sys/types.h>

namespace lab2::handshake {

class HandshakeClient {
public:
    // Сигналы должны быть заблокированы до вызова. Один handshake на процесс.
    [[nodiscard]] std::uint32_t
    connect(pid_t hostPid, std::chrono::milliseconds timeout = common::kIpcTimeout) const;
};

} // namespace lab2::handshake
