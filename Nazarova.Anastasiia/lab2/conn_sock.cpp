#include "connection.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

void Conn::Prepare(pid_t, bool create) {
    Fd socketFd(socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0));
    if (socketFd.get() < 0) systemError("socket");
    sockaddr_un address{};
    address.sun_family = AF_UNIX;
    const std::string path = (directory_.path / "socket").string();
    if (path.size() >= sizeof(address.sun_path)) {
        throw std::runtime_error("Путь к сокету слишком длинный");
    }
    std::memcpy(address.sun_path, path.c_str(), path.size() + 1);
    if (create) {
        if (bind(socketFd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            systemError("bind");
        }
        if (listen(socketFd.get(), 1) < 0) systemError("listen");
        listener_ = std::move(socketFd);
    } else {
        if (connect(socketFd.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) < 0) {
            if (errno != EINPROGRESS) systemError("connect");
            waitReady(socketFd.get(), POLLOUT, Clock::now() + connectionTimeout);
            int error = 0;
            socklen_t size = sizeof(error);
            if (getsockopt(socketFd.get(), SOL_SOCKET, SO_ERROR, &error, &size) < 0) {
                systemError("getsockopt");
            }
            if (error != 0) throw std::runtime_error(std::strerror(error));
        }
        input_ = std::move(socketFd);
        output_.reset(fcntl(input_.get(), F_DUPFD_CLOEXEC, 0));
        if (output_.get() < 0) systemError("fcntl");
    }
}

void Conn::Accept() {
    const auto deadline = Clock::now() + connectionTimeout;
    while (true) {
        waitReady(listener_.get(), POLLIN, deadline);
        input_.reset(accept4(listener_.get(), nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC));
        if (input_.get() >= 0) break;
        if (errno != EINTR && errno != EAGAIN) systemError("accept4");
    }
    output_.reset(fcntl(input_.get(), F_DUPFD_CLOEXEC, 0));
    if (output_.get() < 0) systemError("fcntl");
    listener_.reset();
}
