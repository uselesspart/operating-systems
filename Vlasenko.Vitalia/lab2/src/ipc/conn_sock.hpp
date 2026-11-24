#pragma once

#include "common/constants.hpp"
#include "ipc/connection_factory.hpp"

#include <cstdint>
#include <memory>
#include <sys/types.h>

namespace lab2::ipc {

class SocketConnection final : public IHostConnection {
public:
    // До отправки SIGUSR2: создаёт слушающий сокет и семафоры.
    [[nodiscard]] static std::unique_ptr<SocketConnection>
    prepareHost(std::uint32_t clientId, pid_t clientPid,
                std::chrono::milliseconds timeout = common::kIpcTimeout);

    // После получения SIGUSR2 клиент открывает существующие ресурсы хоста.
    [[nodiscard]] static std::unique_ptr<SocketConnection>
    connectClient(pid_t hostPid, std::uint32_t clientId,
                  std::chrono::milliseconds timeout = common::kIpcTimeout);

    ~SocketConnection() override;
    SocketConnection(const SocketConnection&) = delete;
    SocketConnection& operator=(const SocketConnection&) = delete;

    // После отправки SIGUSR2, в потоке клиентской сессии, а не в потоке handshake.
    void completeHost() override;
    [[nodiscard]] bool isConnected() const noexcept;

    void read(void* buffer, std::size_t count) override;
    void write(const void* buffer, std::size_t count) override;

private:
    struct Impl;
    explicit SocketConnection(std::unique_ptr<Impl> impl) noexcept;
    void requireConnected() const;

    std::unique_ptr<Impl> impl_;
};

} // namespace lab2::ipc
