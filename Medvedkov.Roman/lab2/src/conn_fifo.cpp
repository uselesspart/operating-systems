#include "Connection.hpp"
#include "FileDescriptor.hpp"
#include "SessionResources.hpp"

#include <cerrno>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>

namespace chat {
namespace {
class FifoConnection final : public Connection {
public:
    explicit FifoConnection(SharedSession& sync) : Connection(sync) {}
    ~FifoConnection() override {
        input_.reset();
        output_.reset();
        if (names_.owner()) {
            if (madeToHost_) unlink(names_.file("to-host").c_str());
            if (madeToClient_) unlink(names_.file("to-client").c_str());
        }
    }
    const char* type() const override { return "fifo"; }
    std::string resourceName() const override { return names_.file("to-host"); }
private:
    void createResources() override {
        if (mkfifo(names_.file("to-host").c_str(), 0600) < 0) throw std::system_error(errno, std::generic_category(), "mkfifo host");
        madeToHost_ = true;
        if (mkfifo(names_.file("to-client").c_str(), 0600) < 0) throw std::system_error(errno, std::generic_category(), "mkfifo client");
        madeToClient_ = true;
    }
    void openEndpoint(Party role) override {
        const auto input = names_.file(role == Party::Host ? "to-host" : "to-client");
        const auto output = names_.file(role == Party::Host ? "to-client" : "to-host");
        input_.reset(open(input.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC));
        if (input_.get() < 0) throw std::system_error(errno, std::generic_category(), "open FIFO input");
        const auto limit = deadline();
        for (;;) {
            output_.reset(open(output.c_str(), O_WRONLY | O_NONBLOCK | O_CLOEXEC));
            if (output_.get() >= 0) break;
            if (errno != ENXIO && errno != EINTR) throw std::system_error(errno, std::generic_category(), "open FIFO output");
            if (SharedSession::monotonicNs() >= limit) throw std::runtime_error("FIFO open timeout: 5 seconds");
            usleep(1000);
        }
    }
    void writePacket(const Message& packet) override {
        const auto limit = deadline();
        for (;;) {
            const auto count = write(output_.get(), &packet, sizeof(packet));
            if (count == static_cast<ssize_t>(sizeof(packet))) return;
            if (count >= 0) throw std::runtime_error("Short FIFO write");
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { waitFd(output_.get(), POLLOUT, limit); continue; }
            throw std::system_error(errno, std::generic_category(), "write FIFO");
        }
    }
    Message readPacket() override {
        Message packet;
        std::size_t offset = 0;
        const auto limit = deadline();
        while (offset < sizeof(packet)) {
            const auto count = read(input_.get(), reinterpret_cast<char*>(&packet) + offset, sizeof(packet) - offset);
            if (count > 0) { offset += static_cast<std::size_t>(count); continue; }
            if (count == 0) throw std::runtime_error("FIFO peer closed");
            if (errno == EINTR) continue;
            if (errno == EAGAIN) { waitFd(input_.get(), POLLIN, limit); continue; }
            throw std::system_error(errno, std::generic_category(), "read FIFO");
        }
        return packet;
    }
    SessionResources names_;
    FileDescriptor input_, output_;
    bool madeToHost_ = false;
    bool madeToClient_ = false;
};
}
std::unique_ptr<Connection> makeConnection(SharedSession& sync) {
    return std::make_unique<FifoConnection>(sync);
}
}
