#include "conn.hpp"

#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

bool WaitSocket(int fd, short events, std::chrono::steady_clock::time_point deadline,
                std::string& error) {
    while (true) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            error = "Socket operation timed out after 5 seconds";
            return false;
        }
        pollfd descriptor{};
        descriptor.fd = fd;
        descriptor.events = events;
        const int ready = poll(&descriptor, 1, static_cast<int>(remaining));
        if (ready == 0) {
            error = "Socket operation timed out after 5 seconds";
            return false;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = "poll on socket failed: " + std::string(std::strerror(errno));
            return false;
        }
        if ((descriptor.revents & (POLLNVAL | POLLERR)) != 0) {
            error = "Socket reported an I/O error";
            return false;
        }
        if ((descriptor.revents & events) != 0 || (descriptor.revents & POLLHUP) != 0) {
            return true;
        }
    }
}

bool Transfer(int fd, void* buffer, size_t count, bool writing, std::string& error) {
    size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kTimeoutSeconds);
    while (offset < count) {
        if (!WaitSocket(fd, writing ? POLLOUT : POLLIN, deadline, error)) {
            return false;
        }
        const ssize_t result = writing
            ? send(fd, static_cast<char*>(buffer) + offset, count - offset, MSG_NOSIGNAL)
            : recv(fd, static_cast<char*>(buffer) + offset, count - offset, 0);
        if (result < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            error = (writing ? "send failed: " : "recv failed: ") + std::string(std::strerror(errno));
            return false;
        }
        if (result == 0) {
            error = "Socket was closed by the peer";
            return false;
        }
        offset += static_cast<size_t>(result);
    }
    return true;
}

}

class SocketConn final : public Conn {
public:
    SocketConn(ConnRole role, pid_t host_pid, SharedSync* sync, int inherited_socket)
        : Conn(role, host_pid, sync), connection_fd_(inherited_socket) {
        if (connection_fd_ == -1) {
            throw std::runtime_error("Socket transport requires a socketpair descriptor inherited through fork()");
        }
    }

    ~SocketConn() override {
        if (connection_fd_ != -1) {
            close(connection_fd_);
        }
    }

    bool Read(void* buffer, size_t count) override {
        if (!WaitReadTurn()) {
            return false;
        }
        std::string message;
        return Transfer(connection_fd_, buffer, count, false, message) || Fail(message);
    }

    bool Write(const void* buffer, size_t count) override {
        std::string message;
        if (!Transfer(connection_fd_, const_cast<void*>(buffer), count, true, message)) {
            return Fail(message);
        }
        return PostWriteTurn();
    }

private:
    int connection_fd_;
};

std::unique_ptr<Conn> CreateConn(ConnRole role, pid_t host_pid, SharedSync* sync, int inherited_socket) {
    return std::make_unique<SocketConn>(role, host_pid, sync, inherited_socket);
}
