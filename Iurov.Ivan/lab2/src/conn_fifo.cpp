#include "wolf/transport.hpp"

#include "wolf/protocol.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace wolf {
namespace {

std::string FifoPath(const char* suffix, SharedState* state) {
    return std::string(state->channel_name) + "_" + suffix;
}

int OpenFifo(const std::string& path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kIpcTimeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd != -1) {
            return fd;
        }
        if (errno != ENOENT && errno != EINTR) {
            return -1;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    errno = ETIMEDOUT;
    return -1;
}

int CreateFifo(const std::string& path) {
    if (mkfifo(path.c_str(), 0600) != 0) {
        if (errno != EEXIST) {
            return -1;
        }
        unlink(path.c_str());
        if (mkfifo(path.c_str(), 0600) != 0) {
            return -1;
        }
    }
    return open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
}

bool Transfer(int fd, void* buffer, std::size_t size, bool writing, std::string& error) {
    char* data = static_cast<char*>(buffer);
    std::size_t offset = 0;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kIpcTimeoutSeconds);

    while (offset < size) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now()).count();
        if (left <= 0) {
            error = writing ? "именованный канал: таймаут 5 с при записи"
                            : "именованный канал: таймаут 5 с при чтении";
            return false;
        }

        pollfd descriptor{};
        descriptor.fd = fd;
        descriptor.events = writing ? POLLOUT : POLLIN;
        const int ready = poll(&descriptor, 1, static_cast<int>(left));
        if (ready == 0) {
            error = writing ? "именованный канал: таймаут 5 с при записи"
                            : "именованный канал: таймаут 5 с при чтении";
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
            error = "именованный канал: ошибка ввода-вывода";
            return false;
        }

        const ssize_t result = writing
            ? write(fd, data + offset, size - offset)
            : read(fd, data + offset, size - offset);
        if (result < 0) {
            if (errno == EINTR || errno == EAGAIN) {
                continue;
            }
            error = std::string(writing ? "write: " : "read: ") + std::strerror(errno);
            return false;
        }
        if (result == 0) {
            error = "именованный канал закрыт";
            return false;
        }
        offset += static_cast<std::size_t>(result);
    }
    return true;
}

class FifoConn final : public Conn {
public:
    FifoConn(Role role, SharedState* state) : Conn(role, state) {
        const std::string h2c = FifoPath("h2c", state_);
        const std::string c2h = FifoPath("c2h", state_);

        if (role_ == Role::Host) {
            send_ = CreateFifo(h2c);
            if (send_ == -1) {
                throw std::runtime_error("создание " + h2c + ": " + std::strerror(errno));
            }
            receive_ = CreateFifo(c2h);
            if (receive_ == -1) {
                const int error = errno;
                close(send_);
                send_ = -1;
                unlink(h2c.c_str());
                throw std::runtime_error("создание " + c2h + ": " + std::strerror(error));
            }
            owner_ = true;

            std::string error;
            if (!PostSemaphore(&state_->channel_ready, error)) {
                Cleanup();
                throw std::runtime_error(error);
            }
        } else {
            std::string error;
            if (!WaitSemaphore(&state_->channel_ready, kIpcTimeoutSeconds, error)) {
                throw std::runtime_error("именованные каналы: " + error);
            }
            receive_ = OpenFifo(h2c);
            if (receive_ == -1) {
                throw std::runtime_error("open " + h2c + ": " + std::strerror(errno));
            }
            send_ = OpenFifo(c2h);
            if (send_ == -1) {
                const int error = errno;
                close(receive_);
                receive_ = -1;
                throw std::runtime_error("open " + c2h + ": " + std::strerror(error));
            }
        }
    }

    ~FifoConn() override {
        Cleanup();
    }

    bool Read(void* buffer, std::size_t size) override {
        if (!WaitTurn()) {
            return false;
        }
        std::string error;
        if (!Transfer(receive_, buffer, size, false, error)) {
            return Fail(error);
        }
        return true;
    }

    bool Write(const void* buffer, std::size_t size) override {
        std::string error;
        if (!Transfer(send_, const_cast<void*>(buffer), size, true, error)) {
            return Fail(error);
        }
        return PostTurn();
    }

private:
    void Cleanup() noexcept {
        if (receive_ != -1) {
            close(receive_);
            receive_ = -1;
        }
        if (send_ != -1) {
            close(send_);
            send_ = -1;
        }
        if (owner_) {
            const std::string h2c = FifoPath("h2c", state_);
            const std::string c2h = FifoPath("c2h", state_);
            unlink(h2c.c_str());
            unlink(c2h.c_str());
            owner_ = false;
        }
    }

    int send_ = -1;
    int receive_ = -1;
    bool owner_ = false;
};

}

void PrepareTransport(SharedState* state) {
    std::snprintf(state->channel_name, sizeof(state->channel_name), "/tmp/wolf_%d",
                  static_cast<int>(getpid()));
}

std::unique_ptr<Conn> OpenTransport(Role role, SharedState* state) {
    return std::make_unique<FifoConn>(role, state);
}

}
