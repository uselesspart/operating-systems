#include "Connection.hpp"
#include "SessionResources.hpp"

#include <cerrno>
#include <fcntl.h>
#include <mqueue.h>
#include <poll.h>
#include <stdexcept>
#include <system_error>

namespace chat {
namespace {
class MessageQueueConnection final : public Connection {
public:
    explicit MessageQueueConnection(SharedSession& shared) : Connection(shared) {}
    ~MessageQueueConnection() override {
        if (input_ != -1) mq_close(input_);
        if (output_ != -1) mq_close(output_);
        if (names_.owner()) {
            if (madeToHost_) mq_unlink(names_.queue("host").c_str());
            if (madeToClient_) mq_unlink(names_.queue("client").c_str());
        }
    }
    const char* type() const override { return "mq"; }
    std::string resourceName() const override { return names_.queue("host"); }
private:
    void createResources() override {
        mq_attr attributes{};
        attributes.mq_maxmsg = 1;
        attributes.mq_msgsize = sizeof(Message);
        input_ = mq_open(names_.queue("host").c_str(), O_CREAT | O_EXCL | O_RDONLY | O_NONBLOCK | O_CLOEXEC,
                         0600, &attributes);
        if (input_ == -1) throw std::system_error(errno, std::generic_category(), "mq_open host");
        madeToHost_ = true;
        output_ = mq_open(names_.queue("client").c_str(), O_CREAT | O_EXCL | O_WRONLY | O_NONBLOCK | O_CLOEXEC,
                          0600, &attributes);
        if (output_ == -1) throw std::system_error(errno, std::generic_category(), "mq_open client");
        madeToClient_ = true;
    }
    void openEndpoint(Party role) override {
        if (role == Party::Host) return;
        input_ = mq_open(names_.queue("client").c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (input_ == -1) throw std::system_error(errno, std::generic_category(), "mq_open receive");
        output_ = mq_open(names_.queue("host").c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
        if (output_ == -1) throw std::system_error(errno, std::generic_category(), "mq_open send");
    }
    void writePacket(const Message& packet) override {
        const auto limit = deadline();
        for (;;) {
            if (mq_send(output_, reinterpret_cast<const char*>(&packet), sizeof(packet), 0) == 0) return;
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { waitFd(output_, POLLOUT, limit); continue; }
            throw std::system_error(errno, std::generic_category(), "mq_send");
        }
    }
    Message readPacket() override {
        const auto limit = deadline();
        Message packet;
        for (;;) {
            const auto count = mq_receive(input_, reinterpret_cast<char*>(&packet), sizeof(packet), nullptr);
            if (count == static_cast<ssize_t>(sizeof(packet))) return packet;
            if (count >= 0) throw std::runtime_error("MQ packet has wrong size");
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { waitFd(input_, POLLIN, limit); continue; }
            throw std::system_error(errno, std::generic_category(), "mq_receive");
        }
    }
    SessionResources names_;
    mqd_t input_ = -1;
    mqd_t output_ = -1;
    bool madeToHost_ = false;
    bool madeToClient_ = false;
};
}
std::unique_ptr<Connection> makeConnection(SharedSession& sync) {
    return std::make_unique<MessageQueueConnection>(sync);
}
}
