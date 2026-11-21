#pragma once

#include "../shared_sync.hpp"

#include <sys/types.h>

#include <atomic>
#include <cstddef>
#include <memory>
#include <string>

constexpr int kTimeoutSeconds = 5;

enum class ConnRole {
    Host,
    Client
};

class Conn {
public:
    Conn(ConnRole role, pid_t host_pid, SharedSync* sync);
    virtual ~Conn() = default;

    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    void Cancel() noexcept;
    virtual bool Read(void* buffer, size_t count) = 0;
    virtual bool Write(const void* buffer, size_t count) = 0;

    const std::string& LastError() const noexcept;

protected:
    bool WaitReadTurn();
    bool PostWriteTurn();
    bool Fail(const std::string& message);

    ConnRole role_;
    pid_t host_pid_;
    SharedSync* sync_;
    std::string error_;
    std::atomic<bool> cancelled_{false};
};

std::unique_ptr<Conn> CreateConn(ConnRole role, pid_t host_pid, SharedSync* sync,
                                 int inherited_socket = -1);
