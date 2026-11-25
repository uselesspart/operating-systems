#include "wolf/transport.hpp"

#include "wolf/protocol.hpp"

#include <fcntl.h>
#include <mqueue.h>
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

constexpr mqd_t kInvalidQueue = static_cast<mqd_t>(-1);

std::string QueueName(const char* suffix, SharedState* state) {
    return std::string(state->channel_name) + "_" + suffix;
}

bool MakeDeadline(timespec& deadline, std::string& error) {
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        error = "clock_gettime: " + std::string(std::strerror(errno));
        return false;
    }
    deadline.tv_sec += kIpcTimeoutSeconds;
    return true;
}

mqd_t CreateQueue(const std::string& name) {
    mq_attr attributes{};
    attributes.mq_maxmsg = 8;
    attributes.mq_msgsize = static_cast<long>(sizeof(Message));

    mqd_t queue = mq_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600, &attributes);
    if (queue == kInvalidQueue && errno == EEXIST) {
        mq_unlink(name.c_str());
        queue = mq_open(name.c_str(), O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600, &attributes);
    }
    return queue;
}

mqd_t OpenQueue(const std::string& name) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kIpcTimeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        const mqd_t queue = mq_open(name.c_str(), O_RDWR | O_CLOEXEC);
        if (queue != kInvalidQueue) {
            return queue;
        }
        if (errno != ENOENT && errno != EINTR) {
            return kInvalidQueue;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    errno = ETIMEDOUT;
    return kInvalidQueue;
}

class MqConn final : public Conn {
public:
    MqConn(Role role, SharedState* state) : Conn(role, state) {
        const std::string h2c = QueueName("h2c", state_);
        const std::string c2h = QueueName("c2h", state_);

        if (role_ == Role::Host) {
            send_ = CreateQueue(h2c);
            if (send_ == kInvalidQueue) {
                throw std::runtime_error("mq_open " + h2c + ": " + std::strerror(errno));
            }
            receive_ = CreateQueue(c2h);
            if (receive_ == kInvalidQueue) {
                const int error = errno;
                mq_close(send_);
                send_ = kInvalidQueue;
                mq_unlink(h2c.c_str());
                throw std::runtime_error("mq_open " + c2h + ": " + std::strerror(error));
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
                throw std::runtime_error("очереди сообщений: " + error);
            }
            receive_ = OpenQueue(h2c);
            if (receive_ == kInvalidQueue) {
                throw std::runtime_error("mq_open " + h2c + ": " + std::strerror(errno));
            }
            send_ = OpenQueue(c2h);
            if (send_ == kInvalidQueue) {
                const int error = errno;
                mq_close(receive_);
                receive_ = kInvalidQueue;
                throw std::runtime_error("mq_open " + c2h + ": " + std::strerror(error));
            }
        }
    }

    ~MqConn() override {
        Cleanup();
    }

    bool Read(void* buffer, std::size_t size) override {
        if (!WaitTurn()) {
            return false;
        }
        timespec deadline{};
        std::string error;
        if (!MakeDeadline(deadline, error)) {
            return Fail(error);
        }
        ssize_t received = -1;
        do {
            received = mq_timedreceive(receive_, static_cast<char*>(buffer), size, nullptr, &deadline);
        } while (received == -1 && errno == EINTR);

        if (received == -1) {
            if (errno == ETIMEDOUT) {
                return Fail("очередь сообщений: таймаут 5 с при чтении");
            }
            return Fail("mq_timedreceive: " + std::string(std::strerror(errno)));
        }
        if (static_cast<std::size_t>(received) != size) {
            return Fail("очередь сообщений: неверный размер сообщения");
        }
        return true;
    }

    bool Write(const void* buffer, std::size_t size) override {
        timespec deadline{};
        std::string error;
        if (!MakeDeadline(deadline, error)) {
            return Fail(error);
        }
        int sent = -1;
        do {
            sent = mq_timedsend(send_, static_cast<const char*>(buffer), size, 0, &deadline);
        } while (sent == -1 && errno == EINTR);

        if (sent == -1) {
            if (errno == ETIMEDOUT) {
                return Fail("очередь сообщений: таймаут 5 с при записи");
            }
            return Fail("mq_timedsend: " + std::string(std::strerror(errno)));
        }
        return PostTurn();
    }

private:
    void Cleanup() noexcept {
        if (receive_ != kInvalidQueue) {
            mq_close(receive_);
            receive_ = kInvalidQueue;
        }
        if (send_ != kInvalidQueue) {
            mq_close(send_);
            send_ = kInvalidQueue;
        }
        if (owner_) {
            const std::string h2c = QueueName("h2c", state_);
            const std::string c2h = QueueName("c2h", state_);
            mq_unlink(h2c.c_str());
            mq_unlink(c2h.c_str());
            owner_ = false;
        }
    }

    mqd_t send_ = kInvalidQueue;
    mqd_t receive_ = kInvalidQueue;
    bool owner_ = false;
};

}

void PrepareTransport(SharedState* state) {
    std::snprintf(state->channel_name, sizeof(state->channel_name), "/wolf_%d",
                  static_cast<int>(getpid()));
}

std::unique_ptr<Conn> OpenTransport(Role role, SharedState* state) {
    return std::make_unique<MqConn>(role, state);
}

}
