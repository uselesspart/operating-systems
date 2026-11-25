
#include "connection.h"

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <fcntl.h>
#include <mqueue.h>
#include <memory>
#include <string>
#include <thread>
#include <time.h>

namespace {

constexpr long MAX_MESSAGES = 10;
constexpr auto RETRY_INTERVAL = std::chrono::milliseconds(5);

std::string HostToClient(pid_t pid) {
    return "/lab2_mq_" + std::to_string(pid) + "_h2c";
}

std::string ClientToHost(pid_t pid) {
    return "/lab2_mq_" + std::to_string(pid) + "_c2h";
}

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

class MqConnection final : public Connection {
public:
    MqConnection(ProcessRole role, pid_t pid, SharedSync* sync)
        : role_(role), sync_(sync), pid_(pid) {

        const std::string h2c = HostToClient(pid);
        const std::string c2h = ClientToHost(pid);

        if (role == ProcessRole::Host) {
            mq_attr attr{};
            attr.mq_maxmsg = MAX_MESSAGES;
            attr.mq_msgsize = sizeof(ChatMessage);

            host_to_client_ = mq_open(
                h2c.c_str(),
                O_CREAT | O_RDWR | O_NONBLOCK,
                0600,
                &attr
            );

            client_to_host_ = mq_open(
                c2h.c_str(),
                O_CREAT | O_RDWR | O_NONBLOCK,
                0600,
                &attr
            );
        } else {
            const auto deadline =
                std::chrono::steady_clock::now() +
                std::chrono::seconds(5);

            while (std::chrono::steady_clock::now() < deadline) {
                host_to_client_ = mq_open(
                    h2c.c_str(),
                    O_RDWR | O_NONBLOCK
                );

                client_to_host_ = mq_open(
                    c2h.c_str(),
                    O_RDWR | O_NONBLOCK
                );

                if (IsValid()) {
                    break;
                }

                CloseQueues();

                std::this_thread::sleep_for(
                    std::chrono::milliseconds(50)
                );
            }
        }
    }

    ~MqConnection() override {
        CloseQueues();

        if (role_ == ProcessRole::Host) {
            const std::string h2c = HostToClient(pid_);
            const std::string c2h = ClientToHost(pid_);

            mq_unlink(h2c.c_str());
            mq_unlink(c2h.c_str());
        }
    }

    bool IsValid() const {
        return host_to_client_ != static_cast<mqd_t>(-1) &&
               client_to_host_ != static_cast<mqd_t>(-1);
    }

    bool Read(ChatMessage& message, int timeout_ms) override {
        sem_t* sem = role_ == ProcessRole::Host
            ? &sync_->client_to_host
            : &sync_->host_to_client;

        if (!WaitSemaphore(sem, timeout_ms)) {
            return false;
        }

        mqd_t queue = role_ == ProcessRole::Host
            ? client_to_host_
            : host_to_client_;

        for (;;) {
            const ssize_t result = mq_receive(
                queue,
                reinterpret_cast<char*>(&message),
                sizeof(message),
                nullptr
            );

            if (result == static_cast<ssize_t>(sizeof(message))) {
                return true;
            }

            if (result == -1 && errno == EINTR) {
                continue;
            }

            return false;
        }
    }

    bool Write(const ChatMessage& message, int timeout_ms) override {
        sem_t* sem = role_ == ProcessRole::Host
            ? &sync_->host_to_client
            : &sync_->client_to_host;

        mqd_t queue = role_ == ProcessRole::Host
            ? host_to_client_
            : client_to_host_;

        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(timeout_ms);

        while (true) {
            if (mq_send(
                    queue,
                    reinterpret_cast<const char*>(&message),
                    sizeof(message),
                    0
                ) == 0) {
                return sem_post(sem) == 0;
            }

            if (errno == EINTR) {
                continue;
            }

            if (errno != EAGAIN) {
                return false;
            }

            if (timeout_ms <= 0 ||
                std::chrono::steady_clock::now() >= deadline) {
                return false;
            }

            std::this_thread::sleep_for(RETRY_INTERVAL);
        }
    }

private:
    void CloseQueues() {
        if (host_to_client_ != static_cast<mqd_t>(-1)) {
            mq_close(host_to_client_);
            host_to_client_ = static_cast<mqd_t>(-1);
        }

        if (client_to_host_ != static_cast<mqd_t>(-1)) {
            mq_close(client_to_host_);
            client_to_host_ = static_cast<mqd_t>(-1);
        }
    }

    ProcessRole role_;
    SharedSync* sync_;
    pid_t pid_;

    mqd_t host_to_client_{static_cast<mqd_t>(-1)};
    mqd_t client_to_host_{static_cast<mqd_t>(-1)};
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
    auto connection = std::make_unique<MqConnection>(
        role,
        host_pid,
        sync
    );

    if (!connection->IsValid()) {
        return nullptr;
    }

    return connection;
}
