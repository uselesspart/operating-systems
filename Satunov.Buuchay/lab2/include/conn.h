#pragma once

#include <cstdint>
#include <span>
#include <vector>

class Conn {
public:
    virtual ~Conn() = default;

    // Reads one complete serialized message.
    [[nodiscard]] virtual bool read(
        std::vector<std::uint8_t>& frame
    ) = 0;

    // Sends one complete serialized message.
    [[nodiscard]] virtual bool write(
        std::span<const std::uint8_t> frame
    ) = 0;
};