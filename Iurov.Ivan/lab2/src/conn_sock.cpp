#include "wolf/transport.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace wolf {
namespace {

bool Transfer(int fd, void* buffer, std::size_t size, bool writing, std::string& error) {
    char* data = static_cast<char*>(buffer);
    std::size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kIpcTimeoutSeconds);

    while (offset < size) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            error = writing ? "сокет: таймаут 5 с при записи" : "сокет: таймаут 5 с при чтении";
            return false;
        }

        pollfd descriptor{};
        descriptor.fd = fd;
        descriptor.events = writing ? POLLOUT : POLLIN;
        const int ready = poll(&descriptor, 1, static_cast<int>(left));
        if (ready == 0) {
            error = writing ? "сокет: таймаут 5 с при записи" : "сокет: таймаут 5 с при чтении";
            return false;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = "poll: " + std::string(std::strerror(errno));
            return false;
        }
        if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
            error = "сокет: ошибка ввода-вывода";
            return false;
        }
        if ((descriptor.revents & (POLLIN | POLLOUT | POLLHUP)) == 0) {
            continue;
        }

        const ssize_t result = writing
            ? send(fd, data + offset, size - offset, MSG_NOSIGNAL)
            : recv(fd, data + offset, size - offset, 0);
        if (result < 0) {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }
            error = std::string(writing ? "send: " : "recv: ") + std::strerror(errno);
            return false;
        }
        if (result == 0) {
            error = "сокет закрыт другой стороной";
            return false;
        }
        offset += static_cast<std::size_t>(result);
    }
    return true;
}

class SockConn final : public Conn {
public:
    SockConn(Role role, SharedState* state) : Conn(role, state) {
        const int index = role_ == Role::Host ? 0 : 1;
        fd_ = state_->socket_fds[index];
        if (fd_ < 0) {
            throw std::runtime_error("сокет не подготовлен: нет дескриптора");
        }
        const int other = state_->socket_fds[1 - index];
        if (other >= 0) {
            close(other);
        }
        fcntl(fd_, F_SETFD, FD_CLOEXEC);
    }

    ~SockConn() override {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    bool Read(void* buffer, std::size_t size) override {
        if (!WaitTurn()) {
            return false;
        }
        std::string error;
        if (!Transfer(fd_, buffer, size, false, error)) {
            return Fail(error);
        }
        return true;
    }

    bool Write(const void* buffer, std::size_t size) override {
        std::string error;
        if (!Transfer(fd_, const_cast<void*>(buffer), size, true, error)) {
            return Fail(error);
        }
        return PostTurn();
    }

private:
    int fd_ = -1;
};

}

void PrepareTransport(SharedState* state) {
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, state->socket_fds) != 0) {
        throw std::runtime_error("socketpair: " + std::string(std::strerror(errno)));
    }
}

std::unique_ptr<Conn> OpenTransport(Role role, SharedState* state) {
    return std::make_unique<SockConn>(role, state);
}

}
