#include "conn.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

std::string FifoPath(const char* suffix, pid_t host_pid) {
    return "/tmp/wolf_" + std::string(suffix) + "_" + std::to_string(host_pid);
}

int OpenFifoWithRetry(const std::string& path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kTimeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd != -1) {
            return fd;
        }
        if (errno != ENOENT && errno != EINTR) {
            return -1;
        }
        usleep(100000);
    }
    errno = ETIMEDOUT;
    return -1;
}

bool Transfer(int fd, void* buffer, size_t count, bool writing, std::string& error) {
    size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kTimeoutSeconds);

    while (offset < count) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (remaining <= 0) {
            error = "FIFO operation timed out after 5 seconds";
            return false;
        }

        pollfd descriptor{};
        descriptor.fd = fd;
        descriptor.events = writing ? POLLOUT : POLLIN;
        const int ready = poll(&descriptor, 1, static_cast<int>(remaining));
        if (ready == 0) {
            error = "FIFO operation timed out after 5 seconds";
            return false;
        }
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = "poll on FIFO failed: " + std::string(std::strerror(errno));
            return false;
        }
        if ((descriptor.revents & (POLLERR | POLLNVAL)) != 0) {
            error = "FIFO reported an I/O error";
            return false;
        }

        const ssize_t result = writing
            ? write(fd, static_cast<char*>(buffer) + offset, count - offset)
            : read(fd, static_cast<char*>(buffer) + offset, count - offset);
        if (result < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            error = (writing ? "write to FIFO failed: " : "read from FIFO failed: ")
                + std::string(std::strerror(errno));
            return false;
        }
        if (result == 0) {
            error = "FIFO was closed";
            return false;
        }
        offset += static_cast<size_t>(result);
    }
    return true;
}

}

class FifoConn final : public Conn {
public:
    FifoConn(ConnRole role, pid_t host_pid, SharedSync* sync, int)
        : Conn(role, host_pid, sync), host_to_client_(-1), client_to_host_(-1), owns_fifos_(false) {
        const std::string h2c = FifoPath("h2c", host_pid_);
        const std::string c2h = FifoPath("c2h", host_pid_);

        if (role_ == ConnRole::Host) {
            if (mkfifo(h2c.c_str(), 0600) == -1) {
                throw std::runtime_error("Cannot create FIFO " + h2c + ": " + std::strerror(errno));
            }
            if (mkfifo(c2h.c_str(), 0600) == -1) {
                const int error = errno;
                unlink(h2c.c_str());
                throw std::runtime_error("Cannot create FIFO " + c2h + ": " + std::strerror(error));
            }
            owns_fifos_ = true;
        }

        host_to_client_ = OpenFifoWithRetry(h2c);
        if (host_to_client_ == -1) {
            Cleanup();
            throw std::runtime_error("Cannot open FIFO " + h2c + ": " + std::strerror(errno));
        }
        client_to_host_ = OpenFifoWithRetry(c2h);
        if (client_to_host_ == -1) {
            const int error = errno;
            Cleanup();
            throw std::runtime_error("Cannot open FIFO " + c2h + ": " + std::strerror(error));
        }
    }

    ~FifoConn() override {
        Cleanup();
    }

    bool Read(void* buffer, size_t count) override {
        if (!WaitReadTurn()) {
            return false;
        }
        std::string message;
        const int fd = role_ == ConnRole::Host ? client_to_host_ : host_to_client_;
        return Transfer(fd, buffer, count, false, message) || Fail(message);
    }

    bool Write(const void* buffer, size_t count) override {
        std::string message;
        const int fd = role_ == ConnRole::Host ? host_to_client_ : client_to_host_;
        if (!Transfer(fd, const_cast<void*>(buffer), count, true, message)) {
            return Fail(message);
        }
        return PostWriteTurn();
    }

private:
    void Cleanup() noexcept {
        if (host_to_client_ != -1) {
            close(host_to_client_);
            host_to_client_ = -1;
        }
        if (client_to_host_ != -1) {
            close(client_to_host_);
            client_to_host_ = -1;
        }
        if (owns_fifos_) {
            unlink(FifoPath("h2c", host_pid_).c_str());
            unlink(FifoPath("c2h", host_pid_).c_str());
            owns_fifos_ = false;
        }
    }

    int host_to_client_;
    int client_to_host_;
    bool owns_fifos_;
};

std::unique_ptr<Conn> CreateConn(ConnRole role, pid_t host_pid, SharedSync* sync, int inherited_socket) {
    return std::make_unique<FifoConn>(role, host_pid, sync, inherited_socket);
}
