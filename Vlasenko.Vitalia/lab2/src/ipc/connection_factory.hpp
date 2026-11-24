#pragma once

#include "ipc/connection.hpp"

#include <cstdint>
#include <memory>
#include <sys/types.h>

namespace lab2::ipc {

class IHostConnection : public IConnection {
public:
    virtual void completeHost() = 0;
};

// Реализация выбирается при линковке соответствующего conn_*.cpp.
[[nodiscard]] std::unique_ptr<IHostConnection> prepareHostConnection(std::uint32_t clientId,
                                                                     pid_t clientPid);
[[nodiscard]] std::unique_ptr<IConnection> connectClientConnection(pid_t hostPid,
                                                                   std::uint32_t clientId);

} // namespace lab2::ipc
