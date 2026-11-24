#include "conn_sock.h"

#include "chat_message.h"
#include "transport_name.h"
#include "connection_factory.h"

#include <memory>
#include <mutex>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <system_error>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

const char* transport_name() noexcept {
    return "sock";
}

namespace {

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

class ScopedFd final {
public:
    explicit ScopedFd(int fd) noexcept : fd_(fd) {}
    ~ScopedFd() { if (fd_ != -1) { ::close(fd_); } }

    ScopedFd(const ScopedFd&) = delete;
    ScopedFd& operator=(const ScopedFd&) = delete;

    [[nodiscard]] int get() const noexcept { return fd_; }

private:
    int fd_;
};

constexpr auto kTimeout = std::chrono::seconds(5);

constexpr std::size_t kMaxFrameBytes =
    chat::kHeaderBytes + chat::kMaxTextBytes;

[[noreturn]] void fail(const char* operation) {
    throw std::system_error(
        errno, std::generic_category(), operation
    );
}

bool wait_ready(int fd, short events, Deadline deadline) {
    for (;;) {
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - Clock::now()
            ).count();

        if (remaining <= 0) {
            return false;
        }

        pollfd descriptor{fd, events, 0};

        const int result = poll(
            &descriptor, 1, static_cast<int>(remaining)
        );

        if (result == -1) {
            if (errno == EINTR) {
                continue;
            }

            fail("poll(socket)");
        }

        if (result == 0) {
            return false;
        }

        if ((descriptor.revents & POLLNVAL) != 0) {
            throw std::runtime_error("invalid socket descriptor");
        }

        if ((descriptor.revents & events) != 0) {
            return true;
        }

        if ((descriptor.revents & (POLLERR | POLLHUP)) != 0) {
           return (events & POLLIN) != 0;
       }
    }
}

bool connect_client(int fd, const sockaddr_un& address,
                    Deadline deadline) {
    const auto size = static_cast<socklen_t>(sizeof(address));

    for (;;) {
        if (::connect(
                fd,
                reinterpret_cast<const sockaddr*>(&address),
                size
            ) == 0) {
            return true;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno != EINPROGRESS && errno != EALREADY) {
            fail("connect(socket)");
        }

        if (!wait_ready(fd, POLLOUT, deadline)) {
            return false;
        }

        int error = 0;
        socklen_t error_size = sizeof(error);

        if (getsockopt(
                fd, SOL_SOCKET, SO_ERROR, &error, &error_size
            ) == -1) {
            fail("getsockopt(SO_ERROR)");
        }

        if (error == 0) {
            return true;
        }

        errno = error;
        fail("connect(socket)");
    }
}

enum class ReceiveState {
    Complete,
    TimedOut,
    Disconnected
};

ReceiveState receive_exact(
    int fd,
    std::uint8_t* data,
    std::size_t size,
    Deadline deadline
) {
    std::size_t offset = 0;

    while (offset < size) {
        if (!wait_ready(fd, POLLIN, deadline)) {
            return offset == 0
                ? ReceiveState::TimedOut
                : ReceiveState::Disconnected;
        }

        const ssize_t count = ::recv(
            fd,
            data + offset,
            size - offset,
            0
        );

        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }

        if (count == 0) {
            return ReceiveState::Disconnected;
        }

        if (errno == EINTR ||
            errno == EAGAIN ||
            errno == EWOULDBLOCK) {
            continue;
        }

        fail("recv(socket)");
    }

    return ReceiveState::Complete;
}

bool send_all(int fd, const std::uint8_t* data,
              std::size_t size, Deadline deadline) {
    std::size_t offset = 0;

    while (offset < size) {
        if (!wait_ready(fd, POLLOUT, deadline)) {
            return false;
        }

        const ssize_t count = ::send(
            fd, data + offset, size - offset, MSG_NOSIGNAL
        );

        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }

        if (count == 0) {
            return false;
        }

        if (errno == EINTR || errno == EAGAIN ||
            errno == EWOULDBLOCK) {
            continue;
        }

        if (errno == EPIPE || errno == ECONNRESET) {
            return false;
        }

        fail("send(socket)");
    }

    return true;
}

sockaddr_un make_address(const std::string& path) {
    sockaddr_un address{};
    address.sun_family = AF_UNIX;

    if (path.size() >= sizeof(address.sun_path)) {
        throw std::invalid_argument("socket path is too long");
    }

    std::memcpy(
        address.sun_path,
        path.c_str(),
        path.size() + 1
    );

    return address;
}

} // namespace

SocketConnection::SocketConnection(pid_t host_pid, bool create) {
    if (host_pid <= 0) {
        throw std::invalid_argument("host PID must be positive");
    }

    socket_path_ =
        "/tmp/lab2_sock_" + std::to_string(getuid()) +
        "_" + std::to_string(host_pid) + ".sock";

    const sockaddr_un address = make_address(socket_path_);

    try {
        if (create) {
            listen_fd_ = socket(
                AF_UNIX,
                SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                0
            );

            if (listen_fd_ == -1) {
                fail("socket(server)");
            }

            if (bind(
                    listen_fd_,
                    reinterpret_cast<const sockaddr*>(&address),
                    sizeof(address)
                ) == -1) {
                fail("bind(socket)");
            }

            owns_path_ = true;

            if (listen(listen_fd_, 1) == -1) {
                fail("listen(socket)");
            }
        } else {
            connection_fd_ = socket(
                AF_UNIX,
                SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC,
                0
            );

            if (connection_fd_ == -1) {
                fail("socket(client)");
            }

            if (!connect_client(
                    connection_fd_,
                    address,
                    Clock::now() + kTimeout
                )) {
                throw std::runtime_error(
                    "socket connection timed out"
                );
            }
        }
    } catch (...) {
        cleanup();
        throw;
    }
}

SocketConnection::~SocketConnection() {
    cleanup();
}

void SocketConnection::disconnect_locked() noexcept {
    disconnected_ = true;

    if (connection_fd_ != -1) {
        // shutdown() affects other descriptors that refer to this socket.
        // It wakes a concurrent read/write using a duplicate descriptor.
        ::shutdown(connection_fd_, SHUT_RDWR);
        ::close(connection_fd_);
        connection_fd_ = -1;
    }
}

void SocketConnection::disconnect() noexcept {
    std::lock_guard lock(state_mutex_);
    disconnect_locked();
}

void SocketConnection::cleanup() noexcept {
    std::lock_guard lock(state_mutex_);
    disconnect_locked();

    if (listen_fd_ != -1) {
        ::close(listen_fd_);
        listen_fd_ = -1;
    }

    if (owns_path_) {
        ::unlink(socket_path_.c_str());
        owns_path_ = false;
    }
}

int SocketConnection::acquire_fd(Deadline deadline) {
    std::lock_guard lock(state_mutex_);

    if (!ensure_connected(deadline)) {
        return -1;
    }

    // An operation owns its own descriptor until it completes.
    // Another thread may disconnect the original safely.
    const int copy = ::fcntl(connection_fd_, F_DUPFD_CLOEXEC, 0);

    if (copy == -1) {
        fail("fcntl(F_DUPFD_CLOEXEC)");
    }

    return copy;
}

bool SocketConnection::ensure_connected(Deadline deadline) {
    if (disconnected_) {
        return false;
    }

    if (connection_fd_ != -1) {
        return true;
    }

    if (listen_fd_ == -1) {
        return false;
    }

    if (!wait_ready(listen_fd_, POLLIN, deadline)) {
        return false;
    }

    for (;;) {
        connection_fd_ = accept4(
            listen_fd_,
            nullptr,
            nullptr,
            SOCK_NONBLOCK | SOCK_CLOEXEC
        );

        if (connection_fd_ >= 0) {
            return true;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            if (!wait_ready(listen_fd_, POLLIN, deadline)) {
                return false;
            }
            continue;
        }

        fail("accept4(socket)");
    }
}

bool SocketConnection::read(
    std::vector<std::uint8_t>& frame
) {
    std::lock_guard read_lock(read_mutex_);
    frame.clear();

    const Deadline deadline = Clock::now() + kTimeout;
    const int fd = acquire_fd(deadline);

    if (fd == -1) {
        return false;
    }

    const ScopedFd socket(fd);

    try {
        std::array<std::uint8_t, 4> header{};

        const auto header_result = receive_exact(
            socket.get(), header.data(), header.size(), deadline
        );

        if (header_result == ReceiveState::TimedOut) {
            // No bytes consumed: the connection stays open.
            return false;
        }

        if (header_result != ReceiveState::Complete) {
            throw std::runtime_error(
                "socket disconnected while reading header"
            );
        }

        const std::uint32_t length =
            (static_cast<std::uint32_t>(header[0]) << 24) |
            (static_cast<std::uint32_t>(header[1]) << 16) |
            (static_cast<std::uint32_t>(header[2]) << 8) |
            static_cast<std::uint32_t>(header[3]);

        if (length == 0 || length > kMaxFrameBytes) {
            throw std::runtime_error("invalid socket frame length");
        }

        frame.resize(length);

        const auto body_result = receive_exact(
            socket.get(), frame.data(), frame.size(), deadline
        );

        if (body_result != ReceiveState::Complete) {
            frame.clear();
            throw std::runtime_error(
                "socket disconnected or timed out during frame"
            );
        }

        return true;

    } catch (...) {
        disconnect();
        throw;
    }
}

bool SocketConnection::write(
    std::span<const std::uint8_t> frame
) {
    if (frame.empty() || frame.size() > kMaxFrameBytes) {
        return false;
    }

    std::lock_guard write_lock(write_mutex_);
    const Deadline deadline = Clock::now() + kTimeout;
    const int fd = acquire_fd(deadline);

    if (fd == -1) {
        return false;
    }

    const ScopedFd socket(fd);
    const auto size = static_cast<std::uint32_t>(frame.size());

    const std::array<std::uint8_t, 4> header{
        static_cast<std::uint8_t>(size >> 24),
        static_cast<std::uint8_t>(size >> 16),
        static_cast<std::uint8_t>(size >> 8),
        static_cast<std::uint8_t>(size)
    };

    try {
        const bool sent =
            send_all(
                socket.get(), header.data(), header.size(), deadline
            ) &&
            send_all(
                socket.get(), frame.data(), frame.size(), deadline
            );

        if (!sent) {
            disconnect();
        }

        return sent;

    } catch (...) {
        disconnect();
        throw;
    }
}

std::unique_ptr<Conn>
make_connection(pid_t host_pid, bool create) {
    return std::make_unique<SocketConnection>(host_pid, create);
}
