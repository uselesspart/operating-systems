#pragma once

#include "ipc/connection.hpp"
#include "protocol/messages.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace lab2::protocol {

inline constexpr std::size_t kFrameSize = 24;
inline constexpr std::uint8_t kProtocolVersion = 1;
using Frame = std::array<std::uint8_t, kFrameSize>;

class ProtocolError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Frame encode(const Message& message);
Message decode(const Frame& frame);

void sendMessage(ipc::IConnection& connection, const Message& message);
Message receiveMessage(ipc::IConnection& connection);

} // namespace lab2::protocol
