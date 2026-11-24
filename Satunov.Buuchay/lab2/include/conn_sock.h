#pragma once

#include "conn.h"

#include <chrono>
#include <mutex>
#include <string>
#include <sys/types.h>

class SocketConnection final : public Conn {
public:
    SocketConnection(pid_t host_pid, bool create);
    ~SocketConnection() override;

    SocketConnection(const SocketConnection&) = delete;
    SocketConnection& operator=(const SocketConnection&) = delete;

    [[nodiscard]] bool read(
        std::vector<std::uint8_t>& frame
    ) override;

    [[nodiscard]] bool write(
        std::span<const std::uint8_t> frame
    ) override;

private:
    using Deadline = std::chrono::steady_clock::time_point;

    // Must be called under state_mutex_.
    [[nodiscard]] bool ensure_connected(Deadline deadline);
    [[nodiscard]] int acquire_fd(Deadline deadline);

    void disconnect_locked() noexcept;
    void disconnect() noexcept;
    void cleanup() noexcept;

    // Separate direction locks allow simultaneous reads and writes.
    std::mutex read_mutex_;
    std::mutex write_mutex_;
    std::mutex state_mutex_;

    std::string socket_path_;
    int listen_fd_{-1};
    int connection_fd_{-1};
    bool owns_path_{false};
    bool disconnected_{false};
};
