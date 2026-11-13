#pragma once

#include "protocol.hpp"
#include <memory>

namespace lab {
enum class Role { Host, Client };
// The host creates the object before fork. Both processes call afterFork once.
// One request is followed by one reply; each direction has at most one message.
class Connection {
public:
    virtual ~Connection() = default;
    virtual void afterFork(Role role) = 0;
    virtual Message Read() = 0;
    virtual void Write(const Message& message) = 0;
};
// Each conn_TYPE.cpp supplies this factory and implements the same interface.
std::unique_ptr<Connection> createConnection();
}
