#include "conn_fifo.h"

#include "chat_message.h"
#include "transport_name.h"
#include "connection_factory.h"

#include <memory>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <system_error>
#include <sys/stat.h>
#include <unistd.h>

const char* transport_name() noexcept {
    return "fifo";
}

namespace {

constexpr std::size_t kMaxFrameBytes =
    chat::kHeaderBytes + chat::kMaxTextBytes;

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

[[noreturn]] void fail(const char* operation) {
    throw std::system_error(
        errno, std::generic_category(), operation
    );
}

// One five-second deadline for the entire operation.
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
            fail("poll");
        }

        if (result == 0) {
            return false;
        }

        if ((descriptor.revents & POLLNVAL) != 0 ||
            (descriptor.revents & POLLERR) != 0) {
            throw std::runtime_error("FIFO poll error");
        }

        if ((descriptor.revents & events) != 0) {
            return true;
        }

        if ((descriptor.revents & POLLHUP) != 0) {
            throw std::runtime_error("FIFO peer disconnected");
        }
    }
}

bool read_exact(int fd, std::uint8_t* buffer,
                std::size_t size, Deadline deadline) {
    std::size_t offset = 0;

    while (offset < size) {
        if (!wait_ready(fd, POLLIN, deadline)) {
            if (offset != 0) {
                throw std::runtime_error("incomplete FIFO frame");
            }

            return false;
        }

        const ssize_t count = ::read(
            fd, buffer + offset, size - offset
        );

        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }

        if (count == 0) {
            throw std::runtime_error("FIFO peer disconnected");
        }

        if (errno == EINTR || errno == EAGAIN) {
            continue;
        }

        fail("read(FIFO)");
    }

    return true;
}

bool write_all(int fd, const std::uint8_t* buffer,
               std::size_t size, Deadline deadline) {
    std::size_t offset = 0;

    while (offset < size) {
        if (!wait_ready(fd, POLLOUT, deadline)) {
            return false;
        }

        const ssize_t count = ::write(
            fd, buffer + offset, size - offset
        );

        if (count > 0) {
            offset += static_cast<std::size_t>(count);
            continue;
        }

        if (count == 0) {
            return false;
        }

        if (errno == EINTR || errno == EAGAIN) {
            continue;
        }

        fail("write(FIFO)");
    }

    return true;
}

int open_fifo(const std::string& path, int flags) {
    const int fd = open(
        path.c_str(),
        flags | O_NONBLOCK | O_CLOEXEC | O_NOFOLLOW
    );

    if (fd == -1) {
        fail("open(FIFO)");
    }

    struct stat info{};
    if (fstat(fd, &info) == -1) {
        const int error = errno;
        close(fd);
        throw std::system_error(
            error, std::generic_category(), "fstat(FIFO)"
        );
    }

    if (!S_ISFIFO(info.st_mode)) {
        close(fd);
        throw std::runtime_error("not a FIFO");
    }

    return fd;
}

} // namespace

FifoConnection::FifoConnection(pid_t host_pid, bool create) {
    if (host_pid <= 0) {
        throw std::invalid_argument("host PID must be positive");
    }

    const std::string prefix =
        "/tmp/lab2_fifo_" + std::to_string(getuid()) +
        "_" + std::to_string(host_pid);

    host_to_client_path_ = prefix + "_h2c";
    client_to_host_path_ = prefix + "_c2h";

    try {
        if (create) {
            if (mkfifo(host_to_client_path_.c_str(), 0600) == -1) {
                fail("mkfifo(h2c)");
            }
            created_host_to_client_ = true;

            if (mkfifo(client_to_host_path_.c_str(), 0600) == -1) {
                fail("mkfifo(c2h)");
            }
            created_client_to_host_ = true;
        }

        const auto& input = create
            ? client_to_host_path_ : host_to_client_path_;
        const auto& output = create
            ? host_to_client_path_ : client_to_host_path_;

        // Each process opens descriptors after fork().
        receive_fd_ = open_fifo(input, O_RDONLY);

        // On Linux O_RDWR avoids blocking during opening
        // and prevents SIGPIPE if the peer disappears.
        send_fd_ = open_fifo(output, O_RDWR);
    } catch (...) {
        cleanup();
        throw;
    }
}

FifoConnection::~FifoConnection() {
    cleanup();
}

void FifoConnection::cleanup() noexcept {
    if (receive_fd_ != -1) {
        close(receive_fd_);
        receive_fd_ = -1;
    }

    if (send_fd_ != -1) {
        close(send_fd_);
        send_fd_ = -1;
    }

    if (created_host_to_client_) {
        unlink(host_to_client_path_.c_str());
        created_host_to_client_ = false;
    }

    if (created_client_to_host_) {
        unlink(client_to_host_path_.c_str());
        created_client_to_host_ = false;
    }
}

bool FifoConnection::read(std::vector<std::uint8_t>& frame) {
    frame.clear();

    const Deadline deadline =
        Clock::now() + std::chrono::seconds(5);

    std::array<std::uint8_t, 4> header{};

    if (!read_exact(receive_fd_, header.data(),
                    header.size(), deadline)) {
        return false;
    }

    const std::uint32_t length =
        (static_cast<std::uint32_t>(header[0]) << 24) |
        (static_cast<std::uint32_t>(header[1]) << 16) |
        (static_cast<std::uint32_t>(header[2]) << 8) |
        static_cast<std::uint32_t>(header[3]);

    if (length == 0 || length > kMaxFrameBytes) {
        throw std::runtime_error("invalid FIFO frame length");
    }

    frame.resize(length);

    if (!read_exact(receive_fd_, frame.data(),
                frame.size(), deadline)) {
        frame.clear();
        throw std::runtime_error("FIFO frame body timed out");
    }

    return true;
}

bool FifoConnection::write(std::span<const std::uint8_t> frame) {
    if (frame.empty() || frame.size() > kMaxFrameBytes) {
        return false;
    }

    const auto size = static_cast<std::uint32_t>(frame.size());

    const std::array<std::uint8_t, 4> header{
        static_cast<std::uint8_t>(size >> 24),
        static_cast<std::uint8_t>(size >> 16),
        static_cast<std::uint8_t>(size >> 8),
        static_cast<std::uint8_t>(size)
    };

    const Deadline deadline =
        Clock::now() + std::chrono::seconds(5);

    return write_all(
        send_fd_, header.data(), header.size(), deadline
    ) && write_all(
        send_fd_, frame.data(), frame.size(), deadline
    );
}

std::unique_ptr<Conn>
make_connection(pid_t host_pid, bool create) {
    return std::make_unique<FifoConnection>(host_pid, create);
}