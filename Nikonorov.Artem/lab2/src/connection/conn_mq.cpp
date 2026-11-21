#include "conn.hpp"
#include "../protocol.hpp"

#include <mqueue.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

#include <cerrno>
#include <chrono>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

std::string QueueName(const char* suffix, pid_t host_pid) {
    return "/wolf_" + std::string(suffix) + "_" + std::to_string(host_pid);
}

constexpr mqd_t kInvalidQueue = static_cast<mqd_t>(-1);

bool MakeDeadline(timespec& deadline) {
    if (clock_gettime(CLOCK_REALTIME, &deadline) == -1) {
        return false;
    }
    deadline.tv_sec += kTimeoutSeconds;
    return true;
}

mqd_t OpenQueueWithRetry(const std::string& name) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kTimeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const mqd_t queue = mq_open(name.c_str(), O_RDWR | O_CLOEXEC);
        if (queue != kInvalidQueue) {
            return queue;
        }
        if (errno != ENOENT && errno != EINTR) {
            return kInvalidQueue;
        }
        timespec pause{0, 100000000};
        nanosleep(&pause, nullptr);
    }
    errno = ETIMEDOUT;
    return kInvalidQueue;
}

void CloseQueue(mqd_t& queue) {
    if (queue != kInvalidQueue) {
        mq_close(queue);
        queue = kInvalidQueue;
    }
}

}

class MqConn final : public Conn {
public:
    MqConn(ConnRole role, pid_t host_pid, SharedSync* sync, int)
        : Conn(role, host_pid, sync), host_to_client_(kInvalidQueue), client_to_host_(kInvalidQueue),
          owns_queues_(false) {
        const std::string h2c = QueueName("h2c", host_pid_);
        const std::string c2h = QueueName("c2h", host_pid_);

        if (role_ == ConnRole::Host) {
            mq_attr attributes{};
            attributes.mq_maxmsg = 10;
            attributes.mq_msgsize = static_cast<long>(sizeof(Message));

            host_to_client_ = mq_open(h2c.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC,
                                      0600, &attributes);
            if (host_to_client_ == kInvalidQueue) {
                throw std::runtime_error("Cannot create message queue " + h2c + ": " + std::strerror(errno));
            }
            client_to_host_ = mq_open(c2h.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC,
                                      0600, &attributes);
            if (client_to_host_ == kInvalidQueue) {
                const int error = errno;
                CloseQueue(host_to_client_);
                mq_unlink(h2c.c_str());
                throw std::runtime_error("Cannot create message queue " + c2h + ": " + std::strerror(error));
            }
            owns_queues_ = true;
        } else {
            host_to_client_ = OpenQueueWithRetry(h2c);
            if (host_to_client_ == kInvalidQueue) {
                throw std::runtime_error("Cannot open message queue " + h2c + ": " + std::strerror(errno));
            }
            client_to_host_ = OpenQueueWithRetry(c2h);
            if (client_to_host_ == kInvalidQueue) {
                const int error = errno;
                CloseQueue(host_to_client_);
                throw std::runtime_error("Cannot open message queue " + c2h + ": " + std::strerror(error));
            }
        }
    }

    ~MqConn() override {
        CloseQueue(host_to_client_);
        CloseQueue(client_to_host_);
        if (owns_queues_) {
            mq_unlink(QueueName("h2c", host_pid_).c_str());
            mq_unlink(QueueName("c2h", host_pid_).c_str());
        }
    }

    bool Read(void* buffer, size_t count) override {
        if (!WaitReadTurn()) {
            return false;
        }

        mqd_t queue = role_ == ConnRole::Host ? client_to_host_ : host_to_client_;
        timespec deadline{};
        if (!MakeDeadline(deadline)) {
            return Fail("clock_gettime failed before mq_timedreceive");
        }
        ssize_t received = -1;
        do {
            received = mq_timedreceive(queue, static_cast<char*>(buffer), count, nullptr, &deadline);
        } while (received == -1 && errno == EINTR);
        if (received == -1) {
            return Fail(errno == ETIMEDOUT ? "Message queue receive timed out after 5 seconds"
                                           : "mq_timedreceive failed: " + std::string(std::strerror(errno)));
        }
        if (static_cast<size_t>(received) != count) {
            return Fail("Invalid message size in message queue");
        }
        return true;
    }

    bool Write(const void* buffer, size_t count) override {
        mqd_t queue = role_ == ConnRole::Host ? host_to_client_ : client_to_host_;
        timespec deadline{};
        if (!MakeDeadline(deadline)) {
            return Fail("clock_gettime failed before mq_timedsend");
        }
        int send_result = -1;
        do {
            send_result = mq_timedsend(queue, static_cast<const char*>(buffer), count, 0, &deadline);
        } while (send_result == -1 && errno == EINTR);
        if (send_result == -1) {
            return Fail(errno == ETIMEDOUT ? "Message queue send timed out after 5 seconds"
                                           : "mq_timedsend failed: " + std::string(std::strerror(errno)));
        }
        return PostWriteTurn();
    }

private:
    mqd_t host_to_client_;
    mqd_t client_to_host_;
    bool owns_queues_;
};

std::unique_ptr<Conn> CreateConn(ConnRole role, pid_t host_pid, SharedSync* sync, int inherited_socket) {
    return std::make_unique<MqConn>(role, host_pid, sync, inherited_socket);
}
