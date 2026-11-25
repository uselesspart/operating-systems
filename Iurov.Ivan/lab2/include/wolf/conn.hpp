#pragma once

#include "wolf/shared_state.hpp"

#include <atomic>
#include <cstddef>
#include <string>

namespace wolf {

enum class Role {
    Host,
    Client,
};

class Conn {
public:
    Conn(Role role, SharedState* state);
    virtual ~Conn() = default;

    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    virtual bool Read(void* buffer, std::size_t size) = 0;
    virtual bool Write(const void* buffer, std::size_t size) = 0;

    void Cancel() noexcept;
    const std::string& Error() const noexcept { return error_; }

protected:
    bool WaitTurn();
    bool PostTurn();
    bool Fail(const std::string& message);
    bool Cancelled() const noexcept { return cancelled_.load(); }

    Role role_;
    SharedState* state_;
    std::string error_;
    std::atomic<bool> cancelled_{false};
};

}
