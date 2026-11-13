#include "connection.hpp"
#include "ipc_support.hpp"
#include <sys/mman.h>

namespace lab {
class MmapConnection final : public Connection {
public:
    MmapConnection() : messages_(static_cast<Message*>(mapAnonymous(2 * sizeof(Message)))) {}
    ~MmapConnection() override { munmap(messages_, 2 * sizeof(Message)); }
    void afterFork(Role role) override { role_ = role; }
    Message Read() override {
        waitSemaphore(sync_.incoming(role_));
        return messages_[role_ == Role::Host ? 0 : 1];
    }
    void Write(const Message& message) override {
        messages_[role_ == Role::Host ? 1 : 0] = message;
        postSemaphore(sync_.outgoing(role_));
    }
private:
    Sync sync_;
    Message* messages_;
    Role role_ = Role::Host;
};
std::unique_ptr<Connection> createConnection() { return std::make_unique<MmapConnection>(); }
}
