#pragma once

#include "Message.hpp"
#include <cstdint>
#include <sys/types.h>

namespace chat {
// Only synchronization and ordering metadata are shared; message text uses IPC.
class SharedSession final {
public:
    enum class Gate { Created, Opened, Order, HostEmpty, HostFull, ClientEmpty, ClientFull };
    struct Stamp { std::uint64_t sequence; std::int64_t sentAtNs; };
    static constexpr int TimeoutMs = 5000;

    SharedSession();
    ~SharedSession();
    SharedSession(const SharedSession&) = delete;
    SharedSession& operator=(const SharedSession&) = delete;
    void wait(Gate gate);
    bool tryWait(Gate gate);
    void post(Gate gate);
    Stamp reserve(Party sender);
    void startActivityClock();
    bool clientIdle(std::int64_t nowNs);
    static std::int64_t monotonicNs();
private:
    struct Data;
    Data* data_;
    pid_t owner_;
};
}
