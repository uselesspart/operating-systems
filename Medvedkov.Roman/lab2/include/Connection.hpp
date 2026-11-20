#pragma once

#include "Message.hpp"
#include "SharedSession.hpp"
#include <memory>
#include <optional>
#include <string>

namespace chat {
class Connection {
public:
    explicit Connection(SharedSession& sync) : sync_(sync) {}
    virtual ~Connection() = default;
    Connection(const Connection&) = delete;
    Connection& operator=(const Connection&) = delete;
    void Open(Party role);
    bool Write(const Message& message);
    std::optional<Message> Read();
    void CheckProgress();
    virtual const char* type() const = 0;
    virtual std::string resourceName() const = 0;
protected:
    virtual void createResources() = 0;
    virtual void openEndpoint(Party role) = 0;
    virtual void writePacket(const Message& message) = 0;
    virtual Message readPacket() = 0;
    static void waitFd(int fd, short events, std::int64_t deadline);
    static std::int64_t deadline();
private:
    SharedSession& sync_;
    Party role_ = Party::Everyone;
    std::int64_t lastWrite_ = 0;
};

// Exactly one conn_*.cpp provides this factory for each host_TYPE executable.
std::unique_ptr<Connection> makeConnection(SharedSession& sync);
}
