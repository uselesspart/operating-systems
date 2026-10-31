#include "conn_sock.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/socket.h>
#include <sys/un.h>
#include <thread>
#include <unistd.h>

namespace {
void MakeNonBlocking(int fd) {
    const int flags = fcntl(fd, F_GETFL, 0);
    if (flags != -1) {
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }
}

sockaddr_un MakeAddress(const std::string& path) {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", path.c_str());
    return addr;
}
}

Conn::Conn(Role role, pid_t host_pid, SharedSync* sync)
    : role_(role),
      host_pid_(host_pid),
      sync_(sync),
      fd_(-1),
      listen_fd_(-1),
      socket_path_("/tmp/os_lab2_v13_" + std::to_string(host_pid_) + ".sock"),
      failed_(false) {
    if (socket_path_.size() >= sizeof(sockaddr_un{}.sun_path)) {
        SetError("UNIX socket path is too long");
        return;
    }

    if (role_ == Role::Host) {
        unlink(socket_path_.c_str());
        listen_fd_ = socket(AF_UNIX, SOCK_STREAM, 0);
        if (listen_fd_ == -1) {
            SetError(std::string("socket: ") + std::strerror(errno));
            return;
        }
        const sockaddr_un addr = MakeAddress(socket_path_);
        if (bind(listen_fd_, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == -1) {
            SetError(std::string("bind: ") + std::strerror(errno));
            return;
        }
        if (listen(listen_fd_, 1) == -1) {
            SetError(std::string("listen: ") + std::strerror(errno));
            return;
        }
        MakeNonBlocking(listen_fd_);

        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            fd_ = accept(listen_fd_, nullptr, nullptr);
            if (fd_ != -1) {
                MakeNonBlocking(fd_);
                return;
            }
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                SetError(std::string("accept: ") + std::strerror(errno));
                return;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        SetError("socket client did not connect within 5 seconds");
    } else {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            const int candidate = socket(AF_UNIX, SOCK_STREAM, 0);
            if (candidate == -1) {
                SetError(std::string("socket: ") + std::strerror(errno));
                return;
            }
            const sockaddr_un addr = MakeAddress(socket_path_);
            if (connect(candidate, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) == 0) {
                fd_ = candidate;
                MakeNonBlocking(fd_);
                return;
            }
            close(candidate);
            if (errno != ENOENT && errno != ECONNREFUSED) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        }
        SetError("socket host was not available within 5 seconds");
    }
}

bool Conn::Read(void* buf, std::size_t count) {
    if (failed_ || fd_ == -1) {
        return false;
    }
    if (!TimedLock(&sync_->io_lock)) {
        SetError("local semaphore timeout in Read");
        return false;
    }

    char temp[1024];
    const ssize_t n = recv(fd_, temp, sizeof(temp), MSG_DONTWAIT);
    const int saved_errno = errno;
    Unlock(&sync_->io_lock);

    if (n > 0) {
        read_buffer_.insert(read_buffer_.end(), temp, temp + n);
    } else if (n == 0) {
        SetError("peer closed the socket");
        return false;
    } else if (saved_errno != EAGAIN && saved_errno != EWOULDBLOCK) {
        SetError(std::string("recv: ") + std::strerror(saved_errno));
        return false;
    }

    if (read_buffer_.size() < count) {
        return false;
    }
    std::memcpy(buf, read_buffer_.data(), count);
    read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + static_cast<std::ptrdiff_t>(count));
    return true;
}

bool Conn::Write(const void* buf, std::size_t count) {
    if (failed_ || fd_ == -1) {
        return false;
    }
    if (!TimedLock(&sync_->io_lock)) {
        SetError("local semaphore timeout in Write");
        return false;
    }

    const char* data = static_cast<const char*>(buf);
    std::size_t sent = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (sent < count && std::chrono::steady_clock::now() < deadline) {
        const ssize_t n = send(fd_, data + sent, count - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
        if (n > 0) {
            sent += static_cast<std::size_t>(n);
            continue;
        }
        if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        const int saved_errno = errno;
        Unlock(&sync_->io_lock);
        SetError(std::string("send: ") + std::strerror(saved_errno));
        return false;
    }
    Unlock(&sync_->io_lock);

    if (sent == count) {
        return true;
    }
    SetError("socket send timed out");
    return false;
}

bool Conn::Failed() const {
    return failed_;
}

std::string Conn::Error() const {
    return error_;
}

Conn::~Conn() {
    if (fd_ != -1) {
        close(fd_);
    }
    if (listen_fd_ != -1) {
        close(listen_fd_);
    }
    if (role_ == Role::Host) {
        unlink(socket_path_.c_str());
    }
}

void Conn::SetError(const std::string& message) {
    failed_ = true;
    error_ = message;
    std::cerr << "[sock] " << message << '\n';
}
