#pragma once
#include <chrono>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace application {
using Bytes = std::vector<std::uint8_t>;
inline constexpr auto ipc_timeout = std::chrono::seconds(5);
inline constexpr std::size_t max_packet = 3072;
class IConnection {
public:
    virtual ~IConnection() = default;
    virtual void send(std::span<const std::uint8_t> packet) = 0;
    // Empty means a planned wait for user activity, never a partial packet.
    virtual std::optional<Bytes> receive(std::chrono::milliseconds idle_wait) = 0;
    virtual void close() noexcept = 0;
};
std::shared_ptr<IConnection> connect_to_host(const std::string& root, int client_pid);
} // namespace application
