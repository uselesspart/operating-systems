#pragma once

#include <cstddef>
#include <string_view>

namespace lab2::ipc {

class IConnection {
public:
    virtual ~IConnection() = default;

    virtual void read(void* buffer, std::size_t count) = 0;

    virtual void write(const void* buffer, std::size_t count) = 0;
};

[[nodiscard]] std::string_view transportName() noexcept;

} // namespace lab2::ipc
