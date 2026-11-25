#include "connection.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace {

bool WaitSemaphore(sem_t* sem, int timeout_ms) {
    if (timeout_ms <= 0) {
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
        int remaining = timeout_ms;

        if (timeout_ms >= 0) {
            auto now = std::chrono::steady_clock::now();
            if (now >= deadline) {
                return false;
            }

            remaining = static_cast<int>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    deadline - now
                ).count()
            );

            if (remaining < 1) {
                remaining = 1;
            }
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

        ssize_t count;

        if (writing) {
            count = send(fd, bytes + done, size - done, MSG_NOSIGNAL);
        } else {
            count = recv(fd, bytes + done, size - done, 0);
        }

        if (count < 0 &&
            (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) {
            continue;
        }

        if (count <= 0) {
            return false;
        }

        done += static_cast<std::size_t>(count);
    }

    return true;
}

class SocketConnection final : public Connection {
public:
    SocketConnection(ProcessRole role, SharedSync* sync, int fd)
        : role_(role), sync_(sync), fd_(fd) {}

    ~SocketConnection() override {
        if (fd_ >= 0) {
            close(fd_);
        }
    }

    bool Read(ChatMessage& message, int timeout_ms) override {
        sem_t* sem = role_ == ProcessRole::Host
            ? &sync_->client_to_host
            : &sync_->host_to_client;

        if (!WaitSemaphore(sem, timeout_ms)) {
            return false;
        }

        return Transfer(fd_, &message, sizeof(message), false, 5000);
    }

    bool Write(const ChatMessage& message, int timeout_ms) override {
        sem_t* sem = role_ == ProcessRole::Host
            ? &sync_->host_to_client
            : &sync_->client_to_host;

        if (!Transfer(fd_, const_cast<ChatMessage*>(&message),
                      sizeof(message), true, timeout_ms)) {
            return false;
        }

        return sem_post(sem) == 0;
    }

private:
    ProcessRole role_;
    SharedSync* sync_;
    int fd_;
};

} // namespace

int PrepareConnection(int& host_fd, int& client_fd) {
    int sockets[2];

    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == -1) {
        return -1;
    }

    host_fd = sockets[0];
    client_fd = sockets[1];

    return 0;
}

ConnectionPtr CreateConnection(
    ProcessRole role,
    pid_t,
    SharedSync* sync,
    int fd
) {
    return std::make_unique<SocketConnection>(role, sync, fd);
}