#include "Listener.hpp"
#include "ipc.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace application {
namespace {
Fd new_socket() {
    Fd fd(socket(AF_UNIX, SOCK_STREAM, 0));
    if (fd.get() < 0) { system_error("socket"); }
    if (fcntl(fd.get(), F_SETFD, FD_CLOEXEC) < 0 ||
        fcntl(fd.get(), F_SETFL, O_NONBLOCK) < 0) { system_error("fcntl socket"); }
    return fd;
}
sockaddr_un address(const std::string& path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) { throw Error("Socket path too long"); }
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    return addr;
}
void check_peer(int fd, int expected_pid) {
    ucred credentials{};
    socklen_t size = sizeof(credentials);
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &credentials, &size) < 0) { system_error("SO_PEERCRED"); }
    if (credentials.pid != expected_pid || credentials.uid != getuid()) { throw Error("Socket peer identity mismatch"); }
}
class SocketConnection final : public FramedConnection {
public:
    SocketConnection(std::shared_ptr<ChannelResources> resources, Fd fd, bool host)
        : FramedConnection(std::move(resources), std::move(fd), Fd{}, host) {}
};
class SocketPending final : public PendingConnection {
    std::shared_ptr<ChannelResources> resources_;
    Fd listener_;
    int pid_;
public:
    SocketPending(const std::string& root, int pid)
        : resources_(std::make_shared<ChannelResources>(root, pid, true)),
          listener_(new_socket()), pid_(pid) {
        const auto addr = address(resources_->directory + "/socket");
        if (bind(listener_.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) { system_error("bind"); }
        if (listen(listener_.get(), 1) < 0) { system_error("listen"); }
    }
    std::shared_ptr<IConnection> accept() override {
        if (!wait_fd(listener_.get(), POLLIN, ipc_timeout)) { throw Error("accept timed out after 5 seconds"); }
        Fd fd(::accept(listener_.get(), nullptr, nullptr));
        if (fd.get() < 0) { system_error("accept"); }
        if (fcntl(fd.get(), F_SETFD, FD_CLOEXEC) < 0 ||
            fcntl(fd.get(), F_SETFL, O_NONBLOCK) < 0) { system_error("fcntl accepted socket"); }
        check_peer(fd.get(), pid_);
        return std::make_shared<SocketConnection>(resources_, std::move(fd), true);
    }
};
class SocketListener final : public IListener {
    std::string root_;
public:
    explicit SocketListener(std::string root) : root_(std::move(root)) {}
    std::unique_ptr<PendingConnection> prepare(int pid) override {
        return std::make_unique<SocketPending>(root_, pid);
    }
};
} // namespace
std::unique_ptr<IListener> make_listener(const std::string& root) {
    return std::make_unique<SocketListener>(root);
}
std::shared_ptr<IConnection> connect_to_host(const std::string& root, int pid) {
    auto resources = std::make_shared<ChannelResources>(root, pid, false);
    auto fd = new_socket();
    const auto addr = address(resources->directory + "/socket");
    if (connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) < 0) {
        if (errno != EINPROGRESS && errno != EAGAIN) { system_error("connect"); }
        if (!wait_fd(fd.get(), POLLOUT, ipc_timeout)) { throw Error("connect timed out after 5 seconds"); }
        int error = 0;
        socklen_t size = sizeof(error);
        if (getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &error, &size) < 0) { system_error("SO_ERROR"); }
        if (error) { errno = error; system_error("connect"); }
    }
    const int host_pid = std::stoi(root.substr(root.find_last_of('_') + 1));
    check_peer(fd.get(), host_pid);
    return std::make_shared<SocketConnection>(resources, std::move(fd), false);
}
} // namespace application
