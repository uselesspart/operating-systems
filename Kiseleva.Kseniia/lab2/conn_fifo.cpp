#include "connection.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <string>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {

std::string HostToClient(pid_t pid) {
    return "/tmp/lab2_fifo_" + std::to_string(pid) + "_h2c";
}

std::string ClientToHost(pid_t pid) {
    return "/tmp/lab2_fifo_" + std::to_string(pid) + "_c2h";
}

bool WaitSemaphore(sem_t* sem, int timeout_ms) {
    if (timeout_ms == 0) {
        return sem_trywait(sem) == 0;
    }

    timespec deadline{};
    clock_gettime(CLOCK_REALTIME, &deadline);

    deadline.tv_sec += timeout_ms / 1000;
    deadline.tv_nsec += (timeout_ms % 1000) * 1000000L;

    if (deadline.tv_nsec >= 1000000000L) {
        ++deadline.tv_sec;
        deadline.tv_nsec -= 1000000000L;
    }

    while (sem_timedwait(sem, &deadline) == -1) {
        if (errno == EINTR) {
            continue;
        }
        return false;
    }

    return true;
}

bool Transfer(int fd, void* data, std::size_t size,
              bool writing, int timeout_ms) {
    auto* bytes = static_cast<char*>(data);
    std::size_t done = 0;

    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::milliseconds(timeout_ms);

    while (done < size) {
        auto now = std::chrono::steady_clock::now();

        if (now >= deadline) {
            return false;
        }

        int remaining = static_cast<int>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - now
            ).count()
        );

        if (remaining < 1) {
            remaining = 1;
        }

        pollfd pfd{};
        pfd.fd = fd;
        pfd.events = writing ? POLLOUT : POLLIN;

        int result;
        do {
            result = poll(&pfd, 1, remaining);
        } while (result < 0 && errno == EINTR);

        if (result <= 0 || (pfd.revents & (POLLERR | POLLNVAL))) {
            return false;
        }

        ssize_t count = writing
            ? write(fd, bytes + done, size - done)
            : read(fd, bytes + done, size - done);

        if (count < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        }

        if (count <= 0) {
            return false;
        }

        done += static_cast<std::size_t>(count);
    }

    return true;
}

class FifoConnection final : public Connection {
public:
    FifoConnection(ProcessRole role, pid_t pid, SharedSync* sync)
        : role_(role), sync_(sync), host_pid_(pid) {
        const std::string h2c = HostToClient(pid);
        const std::string c2h = ClientToHost(pid);

        if (role == ProcessRole::Host) {
            mkfifo(h2c.c_str(), 0600);
            mkfifo(c2h.c_str(), 0600);
        }

        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::seconds(5);

        while (std::chrono::steady_clock::now() < deadline) {
            host_to_client_ = open(
                h2c.c_str(), O_RDWR | O_NONBLOCK
            );

            client_to_host_ = open(
                c2h.c_str(), O_RDWR | O_NONBLOCK
            );

            if (host_to_client_ >= 0 && client_to_host_ >= 0) {
                break;
            }

            if (host_to_client_ >= 0) {
                close(host_to_client_);
                host_to_client_ = -1;
            }

            if (client_to_host_ >= 0) {
                close(client_to_host_);
                client_to_host_ = -1;
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(50)
            );
        }
    }

    ~FifoConnection() override {
        if (host_to_client_ >= 0) {
            close(host_to_client_);
        }

        if (client_to_host_ >= 0) {
            close(client_to_host_);
        }

        if (role_ == ProcessRole::Host) {
            unlink(HostToClient(host_pid_).c_str());
            unlink(ClientToHost(host_pid_).c_str());
        }
    }

    bool Read(ChatMessage& message, int timeout_ms) override {
        sem_t* sem = role_ == ProcessRole::Host
            ? &sync_->client_to_host
            : &sync_->host_to_client;

        if (!WaitSemaphore(sem, timeout_ms)) {
            return false;
        }

        int fd = role_ == ProcessRole::Host
            ? client_to_host_
            : host_to_client_;

        return Transfer(fd, &message, sizeof(message), false, 5000);
    }

    bool Write(const ChatMessage& message, int timeout_ms) override {
        sem_t* sem = role_ == ProcessRole::Host
            ? &sync_->host_to_client
            : &sync_->client_to_host;

        int fd = role_ == ProcessRole::Host
            ? host_to_client_
            : client_to_host_;

        ChatMessage copy = message;

        if (!Transfer(fd, &copy, sizeof(copy), true, timeout_ms)) {
            return false;
        }

        return sem_post(sem) == 0;
    }

private:
    ProcessRole role_;
    SharedSync* sync_;
    pid_t host_pid_;
    int host_to_client_{-1};
    int client_to_host_{-1};
};

} // namespace

int PrepareConnection(int&, int&) {
    return 0;
}

ConnectionPtr CreateConnection(
    ProcessRole role,
    pid_t host_pid,
    SharedSync* sync,
    int
) {
    return std::make_unique<FifoConnection>(role, host_pid, sync);
}