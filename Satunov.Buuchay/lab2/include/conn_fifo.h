#pragma once

#include "conn.h"

#include <string>
#include <sys/types.h>

class FifoConnection final : public Conn {
public:
    FifoConnection(pid_t host_pid, bool create);
    ~FifoConnection() override;

    FifoConnection(const FifoConnection&) = delete;
    FifoConnection& operator=(const FifoConnection&) = delete;

    [[nodiscard]] bool read(
        std::vector<std::uint8_t>& frame
    ) override;

    [[nodiscard]] bool write(
        std::span<const std::uint8_t> frame
    ) override;

private:
    void cleanup() noexcept;

    std::string host_to_client_path_;
    std::string client_to_host_path_;

    int receive_fd_{-1};
    int send_fd_{-1};

    bool created_host_to_client_{false};
    bool created_client_to_host_{false};
};