#pragma once

#include <memory>
#include <string>
#include <sys/types.h>
#include "ipc.h"
#include "message.h"

enum class ReadResult { Data, NoData, Closed };

class Conn {
public:
    virtual ~Conn() = default;
    virtual std::string type() const = 0;
    virtual void open(const std::string& directory, bool create) = 0;
    virtual void accept() = 0;
    virtual bool writable() const = 0;
    virtual pid_t peer_pid() const { return 0; }
    virtual void send(const Bytes& packet, const std::atomic_bool& stop) = 0;
    virtual ReadResult receive(Bytes& packet, const std::atomic_bool& stop,
                               int wait_ms = poll_ms, Deadline* deadline = nullptr) = 0;
};

std::unique_ptr<Conn> make_connection();
