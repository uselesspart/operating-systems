#include "conn.h"

#include <cerrno>
#include <fcntl.h>
#include <mqueue.h>
#include <poll.h>
#include <stdexcept>
#include <sys/stat.h>
#include <unistd.h>

namespace {

class Queue {
public:
    Queue() = default;
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    ~Queue() { close(); }
    mqd_t get() const { return value; }
    long size() const { return attributes.mq_msgsize; }

    void open(const std::string& next, bool create, int mode) {
        name = next;
        mq_attr wanted{};
        wanted.mq_maxmsg = 10;
        wanted.mq_msgsize = max_packet;
        int flags = mode | O_NONBLOCK | O_CLOEXEC;
        value = create ? mq_open(name.c_str(), flags | O_CREAT | O_EXCL, 0600, &wanted)
                       : mq_open(name.c_str(), flags);
        if (value == static_cast<mqd_t>(-1)) {
            system_error("Cannot open queue");
        }
        owner = create;
        try {
            struct stat info{};
            if (mq_getattr(value, &attributes) != 0 || fstat(static_cast<int>(value), &info) != 0) {
                system_error("Cannot check queue");
            }
            if (info.st_uid != getuid() || (info.st_mode & 0777) != 0600 ||
                attributes.mq_msgsize != static_cast<long>(max_packet) || attributes.mq_maxmsg != 10) {
                throw std::runtime_error("Invalid queue");
            }
        } catch (...) {
            close();
            throw;
        }
    }

    void close() noexcept {
        if (owner) {
            mq_unlink(name.c_str());
            owner = false;
        }
        if (value != static_cast<mqd_t>(-1)) {
            mq_close(value);
            value = static_cast<mqd_t>(-1);
        }
    }

private:
    mqd_t value = static_cast<mqd_t>(-1);
    mq_attr attributes{};
    std::string name;
    bool owner = false;
};

}

class MqConn final : public Conn {
public:
    std::string type() const override { return "mq"; }
    bool writable() const override { return can_write(static_cast<int>(output.get())); }

    void open(const std::string& directory, bool create) override {
        try {
            input.open(queue_name(directory, create ? "up" : "down"), create, O_RDONLY);
            output.open(queue_name(directory, create ? "down" : "up"), create, O_WRONLY);
        } catch (...) {
            input.close();
            output.close();
            throw;
        }
    }

    void accept() override {}

    void send(const Bytes& packet, const std::atomic_bool& stop) override {
        if (packet.size() < header_size || packet.size() > max_packet) {
            throw std::runtime_error("Invalid outgoing packet");
        }
        auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        while (true) {
            check_stop(stop);
            if (Clock::now() >= deadline) {
                throw std::runtime_error("Write timeout");
            }
            if (mq_send(output.get(), packet.data(), packet.size(), 0) == 0) {
                return;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN) {
                system_error("Cannot send queue");
            }
            pollfd event{static_cast<int>(output.get()), POLLOUT, 0};
            if (poll(&event, 1, remaining_ms(deadline)) < 0 && errno != EINTR) {
                system_error("Cannot wait for queue");
            }
        }
    }

    ReadResult receive(Bytes& packet, const std::atomic_bool& stop, int wait_ms,
                       Deadline* deadline) override {
        Bytes bytes(input.size());
        auto until = Clock::now() + std::chrono::milliseconds(wait_ms);
        bool polled = false;
        while (true) {
            check_stop(stop);
            unsigned priority = 0;
            ssize_t count = mq_receive(input.get(), bytes.data(), bytes.size(), &priority);
            if (count >= 0) {
                if (count < static_cast<ssize_t>(header_size) || priority != 0) {
                    throw std::runtime_error("Invalid queue packet");
                }
                bytes.resize(static_cast<std::size_t>(count));
                packet = std::move(bytes);
                if (deadline) {
                    *deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
                }
                return ReadResult::Data;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno != EAGAIN) {
                system_error("Cannot receive queue");
            }
            if (polled && Clock::now() >= until) {
                return ReadResult::NoData;
            }
            pollfd event{static_cast<int>(input.get()), POLLIN, 0};
            if (poll(&event, 1, remaining_ms(until)) < 0 && errno != EINTR) {
                system_error("Cannot wait for queue");
            }
            polled = true;
        }
    }

private:
    Queue input;
    Queue output;
};

std::unique_ptr<Conn> make_connection() { return std::make_unique<MqConn>(); }
