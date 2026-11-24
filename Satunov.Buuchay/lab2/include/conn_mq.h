#pragma once

#include "conn.h"

#include <mqueue.h>
#include <string>
#include <sys/types.h>

class MqConnection final : public Conn {
public:
    MqConnection(pid_t host_pid, bool create);
    ~MqConnection() override;

    MqConnection(const MqConnection&) = delete;
    MqConnection& operator=(const MqConnection&) = delete;

    [[nodiscard]] bool read(
        std::vector<std::uint8_t>& frame
    ) override;

    [[nodiscard]] bool write(
        std::span<const std::uint8_t> frame
    ) override;

private:
    void cleanup() noexcept;

    std::string host_to_client_name_;
    std::string client_to_host_name_;

    mqd_t receive_{static_cast<mqd_t>(-1)};
    mqd_t send_{static_cast<mqd_t>(-1)};

    bool created_host_to_client_{false};
    bool created_client_to_host_{false};
};