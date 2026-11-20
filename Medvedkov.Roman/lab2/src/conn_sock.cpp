#include "Connection.hpp"
#include "FileDescriptor.hpp"

#include <cerrno>
#include <poll.h>
#include <stdexcept>
#include <system_error>
#include <sys/socket.h>

namespace chat {
namespace {
class SocketConnection final : public Connection {
public:
    explicit SocketConnection(SharedSession& sync) : Connection(sync) {
        int descriptors[2];
        if (socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, descriptors) < 0) {
            throw std::system_error(errno, std::generic_category(), "socketpair");
        }
        host_.reset(descriptors[0]);
        client_.reset(descriptors[1]);
    }
    const char* type() const override { return "sock"; }
    std::string resourceName() const override { return {}; }
private:
    void createResources() override {} // Related processes inherit socketpair.
    void openEndpoint(Party role) override {
        if (role == Party::Host) { client_.reset(); fd_ = host_.get(); }
        else { host_.reset(); fd_ = client_.get(); }
    }
    void writePacket(const Message& packet) override {
        const auto limit = deadline();
        for (;;) {
            const auto count = send(fd_, &packet, sizeof(packet), MSG_NOSIGNAL);
            if (count == static_cast<ssize_t>(sizeof(packet))) return;
            if (count >= 0) throw std::runtime_error("Short socket packet");
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { waitFd(fd_, POLLOUT, limit); continue; }
            throw std::system_error(errno, std::generic_category(), "send socket");
        }
    }
    Message readPacket() override {
        Message packet;
        const auto limit = deadline();
        for (;;) {
            const auto count = recv(fd_, &packet, sizeof(packet), MSG_TRUNC);
            if (count == static_cast<ssize_t>(sizeof(packet))) return packet;
            if (count >= 0) throw std::runtime_error("Socket closed or invalid packet size");
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { waitFd(fd_, POLLIN, limit); continue; }
            throw std::system_error(errno, std::generic_category(), "recv socket");
        }
    }
    FileDescriptor host_, client_;
    int fd_ = -1;
};
}
std::unique_ptr<Connection> makeConnection(SharedSession& sync) {
    return std::make_unique<SocketConnection>(sync);
}
}
