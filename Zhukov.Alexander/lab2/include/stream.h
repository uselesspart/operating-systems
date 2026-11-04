#pragma once

#include "conn.h"

class Stream {
public:
    void send(int fd, const Bytes& packet, const std::atomic_bool& stop, bool socket);
    ReadResult receive(int fd, Bytes& packet, const std::atomic_bool& stop,
                       int wait_ms, Deadline* deadline);

private:
    Bytes buffer;
    Deadline partial_deadline{};
};
