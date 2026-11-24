#pragma once

#include "common/constants.hpp"
#include "ipc/connection_factory.hpp"

#include <cstdint>
#include <memory>
#include <sys/types.h>

namespace lab2::ipc {

class FifoConnection final : public IHostConnection {
public:
    // До отправки SIGUSR2: создаёт оба FIFO и семафоры, открывает входной конец.
    [[nodiscard]] static std::unique_ptr<FifoConnection>
    prepareHost(std::uint32_t clientId, std::chrono::milliseconds timeout = common::kIpcTimeout);

    // После получения SIGUSR2 клиент открывает существующие ресурсы хоста.
    [[nodiscard]] static std::unique_ptr<FifoConnection>
    connectClient(pid_t hostPid, std::uint32_t clientId,
                  std::chrono::milliseconds timeout = common::kIpcTimeout);

    ~FifoConnection() override;
    FifoConnection(const FifoConnection&) = delete;
    FifoConnection& operator=(const FifoConnection&) = delete;

    // После отправки SIGUSR2, в потоке клиентской сессии, а не в потоке handshake.
    void completeHost() override;
    [[nodiscard]] bool isConnected() const noexcept;

    void read(void* buffer, std::size_t count) override;
    void write(const void* buffer, std::size_t count) override;

private:
    struct Impl;
    explicit FifoConnection(std::unique_ptr<Impl> impl) noexcept;
    void requireConnected() const;

    std::unique_ptr<Impl> impl_;
};

} // namespace lab2::ipc
