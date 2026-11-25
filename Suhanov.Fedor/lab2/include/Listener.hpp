#pragma once
#include "Connection.hpp"

namespace application {
class PendingConnection {
public:
    virtual ~PendingConnection() = default;
    virtual std::shared_ptr<IConnection> accept() = 0;
};
class IListener {
public:
    virtual ~IListener() = default;
    // Creates the personal channel and semaphores BEFORE SIGUSR2.
    virtual std::unique_ptr<PendingConnection> prepare(int client_pid) = 0;
};
std::unique_ptr<IListener> make_listener(const std::string& root);
} // namespace application
