#pragma once

#include "common/constants.hpp"
#include "common/named_semaphore.hpp"
#include "common/unique_fd.hpp"
#include "ipc/connection.hpp"

namespace lab2::ipc {

// Один поток обслуживает соединение. Размеры соответствующих вызовов read/write
// должны совпадать: семафор отмечает готовность очередного блока данных.
class SynchronizedStream final : public IConnection {
public:
    SynchronizedStream(common::UniqueFd reader, common::UniqueFd writer,
                       common::NamedSemaphore incoming, common::NamedSemaphore outgoing,
                       std::chrono::milliseconds timeout = common::kIpcTimeout);

    void attachWriter(common::UniqueFd writer);
    [[nodiscard]] bool isConnected() const noexcept;

    void read(void* buffer, std::size_t count) override;
    void write(const void* buffer, std::size_t count) override;

private:
    void requireConnected() const;
    void close() noexcept;

    common::UniqueFd reader_;
    common::UniqueFd writer_;
    common::NamedSemaphore incoming_;
    common::NamedSemaphore outgoing_;
    std::chrono::milliseconds timeout_;
};

} // namespace lab2::ipc
