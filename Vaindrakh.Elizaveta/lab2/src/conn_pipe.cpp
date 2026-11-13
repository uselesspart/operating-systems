#include "connection.hpp"
#include "ipc_support.hpp"
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace lab {
class PipeConnection final : public Connection {
public:
    PipeConnection() {
        if (pipe2(toHost_, O_CLOEXEC | O_NONBLOCK) == -1) systemError("pipe to host");
        if (pipe2(toClient_, O_CLOEXEC | O_NONBLOCK) == -1) {
            close(toHost_[0]); close(toHost_[1]);
            systemError("pipe to client");
        }
    }
    ~PipeConnection() override {
        for (int fd : toHost_) if (fd != -1) close(fd);
        for (int fd : toClient_) if (fd != -1) close(fd);
    }
    void afterFork(Role role) override {
        role_ = role;
        closeEnd(role == Role::Host ? toHost_[1] : toHost_[0]);
        closeEnd(role == Role::Host ? toClient_[0] : toClient_[1]);
        input_ = role == Role::Host ? toHost_[0] : toClient_[0];
        output_ = role == Role::Host ? toClient_[1] : toHost_[1];
    }
    Message Read() override {
        waitSemaphore(sync_.incoming(role_));
        Message message{};
        transfer(input_, &message, false);
        return message;
    }
    void Write(const Message& message) override {
        Message copy = message;
        transfer(output_, &copy, true);
        postSemaphore(sync_.outgoing(role_));
    }
private:
    static void closeEnd(int& fd) { close(fd); fd = -1; }
    static void transfer(int fd, Message* message, bool writing) {
        auto* bytes = reinterpret_cast<char*>(message);
        std::size_t offset = 0;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
        while (offset < sizeof(Message)) {
            const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
            if (left <= 0) throw std::runtime_error("Pipe timeout after 5 seconds");
            pollfd descriptor{fd, static_cast<short>(writing ? POLLOUT : POLLIN), 0};
            const int ready = poll(&descriptor, 1, static_cast<int>(left));
            if (ready == -1 && errno == EINTR) continue;
            if (ready == -1) systemError("poll pipe");
            if (ready == 0) throw std::runtime_error("Pipe timeout after 5 seconds");
            const ssize_t count = writing ? write(fd, bytes + offset, sizeof(Message) - offset)
                                          : read(fd, bytes + offset, sizeof(Message) - offset);
            if (count == -1 && (errno == EINTR || errno == EAGAIN)) continue;
            if (count == -1) systemError(writing ? "write pipe" : "read pipe");
            if (count == 0) throw std::runtime_error("Pipe peer disconnected");
            offset += static_cast<std::size_t>(count);
        }
    }
    Sync sync_;
    int toHost_[2] = {-1, -1};
    int toClient_[2] = {-1, -1};
    int input_ = -1;
    int output_ = -1;
    Role role_ = Role::Host;
};
std::unique_ptr<Connection> createConnection() { return std::make_unique<PipeConnection>(); }
}
