#include "connection.hpp"
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <utility>

namespace chat {
Fd::~Fd() { if (value_ >= 0) close(value_); }
Fd::Fd(Fd&& other) noexcept : value_(other.release()) {}
Fd& Fd::operator=(Fd&& other) noexcept {
    if (this != &other) { if (value_ >= 0) close(value_); value_ = other.release(); }
    return *this;
}
int Fd::release() { return std::exchange(value_, -1); }
void require(bool condition, const std::string& operation) {
    if (!condition) throw std::runtime_error(operation + ": " + std::strerror(errno));
}
void nonblocking(int fd) {
    const int flags = fcntl(fd, F_GETFL);
    require(flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0, "fcntl");
}
void wait_fd(int fd, short events, Clock::time_point deadline) {
    for (;;) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (remaining <= 0) throw std::runtime_error("I/O timeout (5 seconds)");
        pollfd p{fd, events, 0};
        const int result = poll(&p, 1, static_cast<int>(remaining));
        if (result < 0 && errno == EINTR) continue;
        require(result >= 0, "poll");
        if (result == 0) continue;
        if ((p.revents & events) != 0) return;
        throw std::runtime_error("Peer disconnected");
    }
}
static sockaddr_un address(const std::string& path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    if (path.size() >= sizeof(addr.sun_path)) throw std::runtime_error("Socket path too long");
    std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
    return addr;
}
Fd listener(const std::string& path) {
    Fd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    require(fd.get() >= 0, "socket");
    const auto addr = address(path);
    require(bind(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0, "bind");
    require(listen(fd.get(), 8) == 0, "listen");
    return fd;
}
Fd connect_socket(const std::string& path) {
    Fd fd(socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK, 0));
    require(fd.get() >= 0, "socket");
    const auto addr = address(path);
    const int result = connect(fd.get(), reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
    if (result != 0) {
        require(errno == EINPROGRESS, "connect");
        wait_fd(fd.get(), POLLOUT, Clock::now() + std::chrono::seconds(timeout_seconds));
        int error = 0;
        socklen_t size = sizeof(error);
        require(getsockopt(fd.get(), SOL_SOCKET, SO_ERROR, &error, &size) == 0, "SO_ERROR");
        errno = error;
        require(error == 0, "connect");
    }
    return fd;
}
Fd accept_peer(int listener_fd, int expected_pid) {
    wait_fd(listener_fd, POLLIN, Clock::now() + std::chrono::seconds(timeout_seconds));
    Fd fd(accept4(listener_fd, nullptr, nullptr, SOCK_CLOEXEC | SOCK_NONBLOCK));
    require(fd.get() >= 0, "accept");
    ucred credential{};
    socklen_t size = sizeof(credential);
    require(getsockopt(fd.get(), SOL_SOCKET, SO_PEERCRED, &credential, &size) == 0, "SO_PEERCRED");
    if (credential.pid != expected_pid || credential.uid != geteuid()) throw std::runtime_error("Unexpected peer credentials");
    return fd;
}
void send_fds(int socket_fd, int read_fd, int write_fd) {
    alignas(cmsghdr) char control[CMSG_SPACE(2 * sizeof(int))]{};
    char byte = 'P';
    iovec io{&byte, 1};
    msghdr message{};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    cmsghdr* header = CMSG_FIRSTHDR(&message);
    header->cmsg_level = SOL_SOCKET;
    header->cmsg_type = SCM_RIGHTS;
    header->cmsg_len = CMSG_LEN(2 * sizeof(int));
    const int fds[] = {read_fd, write_fd};
    std::memcpy(CMSG_DATA(header), fds, sizeof(fds));
    wait_fd(socket_fd, POLLOUT, Clock::now() + std::chrono::seconds(timeout_seconds));
    require(sendmsg(socket_fd, &message, MSG_NOSIGNAL) == 1, "sendmsg(SCM_RIGHTS)");
}
std::pair<Fd, Fd> receive_fds(int socket_fd) {
    alignas(cmsghdr) char control[CMSG_SPACE(2 * sizeof(int))]{};
    char byte = 0;
    iovec io{&byte, 1};
    msghdr message{};
    message.msg_iov = &io;
    message.msg_iovlen = 1;
    message.msg_control = control;
    message.msg_controllen = sizeof(control);
    wait_fd(socket_fd, POLLIN, Clock::now() + std::chrono::seconds(timeout_seconds));
    require(recvmsg(socket_fd, &message, MSG_CMSG_CLOEXEC) == 1, "recvmsg(SCM_RIGHTS)");
    const auto* header = CMSG_FIRSTHDR(&message);
    if (!header || header->cmsg_level != SOL_SOCKET || header->cmsg_type != SCM_RIGHTS ||
        header->cmsg_len != CMSG_LEN(2 * sizeof(int))) throw std::runtime_error("Invalid pipe descriptors");
    int fds[2];
    std::memcpy(fds, CMSG_DATA(header), sizeof(fds));
    Fd first(fds[0]), second(fds[1]);
    if ((message.msg_flags & MSG_CTRUNC) != 0 || byte != 'P') throw std::runtime_error("Truncated descriptor packet");
    return {std::move(first), std::move(second)};
}
std::string session_path(int host) {
    return "/tmp/gutsalov-lab2-" + std::to_string(geteuid()) + "-" + std::to_string(host);
}
std::string endpoint_path(int host, int client) {
    return session_path(host) + "/" + transport_name() + "-" + std::to_string(client);
}

Channel::Channel(int host, int client, bool owner)
    : connection_(make_connection(host, client, owner)), owner_(owner),
      prefix_("/gutsalov-l2-" + std::to_string(geteuid()) + "-" + std::to_string(host) +
              "-" + transport_name() + "-" + std::to_string(client)),
      read_index_(owner ? 0 : 2), write_index_(owner ? 2 : 0) {}
Channel::~Channel() {
    for (int i = 0; i < 4; ++i) {
        if (semaphores_[i] != SEM_FAILED) sem_close(semaphores_[i]);
        if (owner_) sem_unlink((prefix_ + "-" + std::to_string(i)).c_str());
    }
}
void Channel::open_semaphores() {
    for (int i = 0; i < 4; ++i) {
        semaphores_[i] = sem_open((prefix_ + "-" + std::to_string(i)).c_str(),
                                  owner_ ? O_CREAT | O_EXCL : 0, 0600, i % 2 == 0 ? 1 : 0);
        require(semaphores_[i] != SEM_FAILED, "sem_open");
    }
}
void Channel::prepare() { connection_->prepare(); if (owner_) open_semaphores(); }
void Channel::open() {
    if (!owner_) open_semaphores();
    auto descriptors = connection_->open();
    read_ = std::move(descriptors.first);
    write_ = std::move(descriptors.second);
}
static void wait_sem(sem_t* semaphore) {
    timespec deadline{};
    require(clock_gettime(CLOCK_MONOTONIC, &deadline) == 0, "clock_gettime");
    deadline.tv_sec += timeout_seconds;
    int result;
    do { result = sem_clockwait(semaphore, CLOCK_MONOTONIC, &deadline); } while (result != 0 && errno == EINTR);
    require(result == 0, "sem_clockwait (5 seconds)");
}
static void transfer(int fd, void* buffer, std::size_t size, bool writing) {
    const auto deadline = Clock::now() + std::chrono::seconds(timeout_seconds);
    auto* bytes = static_cast<char*>(buffer);
    while (size > 0) {
        wait_fd(fd, writing ? POLLOUT : POLLIN, deadline);
        const auto amount = writing ? ::write(fd, bytes, size) : ::read(fd, bytes, size);
        if (amount < 0 && (errno == EINTR || errno == EAGAIN)) continue;
        require(amount >= 0, writing ? "write" : "read");
        if (amount == 0) throw std::runtime_error("Peer disconnected");
        bytes += amount;
        size -= static_cast<std::size_t>(amount);
    }
}
void Channel::write(const Message& message) {
    auto payload = encode(message);
    std::uint32_t length = static_cast<std::uint32_t>(payload.size());
    std::string frame(sizeof(length), '\0');
    std::memcpy(frame.data(), &length, sizeof(length));
    frame += payload;
    std::lock_guard lock(writer_mutex_);
    wait_sem(semaphores_[write_index_]);
    transfer(write_.get(), frame.data(), frame.size(), true);
    require(sem_post(semaphores_[write_index_ + 1]) == 0, "sem_post(ready)");
}
bool Channel::read(Message& message, const std::atomic<bool>& stop) {
    while (!stop.load()) {
        pollfd descriptor{read_.get(), POLLIN, 0};
        const int result = poll(&descriptor, 1, 100);
        if (result < 0 && errno == EINTR) continue;
        require(result >= 0, "poll(idle)");
        if (result == 0) return false;
        if ((descriptor.revents & POLLIN) == 0) throw std::runtime_error("Peer disconnected");
        wait_sem(semaphores_[read_index_ + 1]);
        std::uint32_t length = 0;
        transfer(read_.get(), &length, sizeof(length), false);
        if (length == 0 || length > max_text + 128) throw std::runtime_error("Invalid frame size");
        std::string payload(length, '\0');
        transfer(read_.get(), payload.data(), payload.size(), false);
        require(sem_post(semaphores_[read_index_]) == 0, "sem_post(slot)");
        message = decode(payload);
        return true;
    }
    return false;
}
} // namespace chat
