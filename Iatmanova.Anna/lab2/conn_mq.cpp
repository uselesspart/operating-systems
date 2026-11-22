#include "conn_mq.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <thread>

namespace {
constexpr long kMaxMessages = 10;
constexpr long kMessageSize = static_cast<long>(sizeof(ChatPacket));
}

Conn::Conn(Role role, pid_t host_pid, SharedSync* sync)
    : role_(role),
      host_pid_(host_pid),
      sync_(sync),
      incoming_(static_cast<mqd_t>(-1)),
      outgoing_(static_cast<mqd_t>(-1)),
      h2c_name_("/os_lab2_v13_" + std::to_string(host_pid_) + "_h2c"),
      c2h_name_("/os_lab2_v13_" + std::to_string(host_pid_) + "_c2h"),
      failed_(false) {
    mq_attr attr{};
    attr.mq_flags = 0;
    attr.mq_maxmsg = kMaxMessages;
    attr.mq_msgsize = kMessageSize;

    if (role_ == Role::Host) {
        mq_unlink(h2c_name_.c_str());
        mq_unlink(c2h_name_.c_str());

        mqd_t h2c = mq_open(h2c_name_.c_str(), O_CREAT | O_RDWR | O_NONBLOCK, 0600, &attr);
        if (h2c == static_cast<mqd_t>(-1)) {
            SetError(std::string("mq_open h2c: ") + std::strerror(errno));
            return;
        }
        mqd_t c2h = mq_open(c2h_name_.c_str(), O_CREAT | O_RDWR | O_NONBLOCK, 0600, &attr);
        if (c2h == static_cast<mqd_t>(-1)) {
            mq_close(h2c);
            SetError(std::string("mq_open c2h: ") + std::strerror(errno));
            return;
        }
        outgoing_ = h2c;
        incoming_ = c2h;
    } else {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        while (std::chrono::steady_clock::now() < deadline) {
            mqd_t h2c = mq_open(h2c_name_.c_str(), O_RDWR | O_NONBLOCK);
            mqd_t c2h = mq_open(c2h_name_.c_str(), O_RDWR | O_NONBLOCK);
            if (h2c != static_cast<mqd_t>(-1) && c2h != static_cast<mqd_t>(-1)) {
                incoming_ = h2c;
                outgoing_ = c2h;
                return;
            }
            if (h2c != static_cast<mqd_t>(-1)) {
                mq_close(h2c);
            }
            if (c2h != static_cast<mqd_t>(-1)) {
                mq_close(c2h);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
        SetError("message queues were not created within 5 seconds");
    }
}

bool Conn::Read(void* buf, std::size_t count) {
    if (failed_ || incoming_ == static_cast<mqd_t>(-1) || count > kMessageSize) {
        return false;
    }
    if (!TimedLock(&sync_->io_lock)) {
        SetError("local semaphore timeout in Read");
        return false;
    }
    const ssize_t n = mq_receive(incoming_, static_cast<char*>(buf), count, nullptr);
    const int saved_errno = errno;
    Unlock(&sync_->io_lock);

    if (n == static_cast<ssize_t>(count)) {
        return true;
    }
    if (n == -1 && saved_errno == EAGAIN) {
        return false;
    }
    if (n == -1) {
        SetError(std::string("mq_receive: ") + std::strerror(saved_errno));
    } else {
        SetError("mq_receive returned an unexpected message size");
    }
    return false;
}

bool Conn::Write(const void* buf, std::size_t count) {
    if (failed_ || outgoing_ == static_cast<mqd_t>(-1) || count > kMessageSize) {
        return false;
    }
    if (!TimedLock(&sync_->io_lock)) {
        SetError("local semaphore timeout in Write");
        return false;
    }
    const int rc = mq_send(outgoing_, static_cast<const char*>(buf), count, 0);
    const int saved_errno = errno;
    Unlock(&sync_->io_lock);

    if (rc == 0) {
        return true;
    }
    SetError(std::string("mq_send: ") + std::strerror(saved_errno));
    return false;
}

bool Conn::Failed() const {
    return failed_;
}

std::string Conn::Error() const {
    return error_;
}

Conn::~Conn() {
    if (incoming_ != static_cast<mqd_t>(-1)) {
        mq_close(incoming_);
    }
    if (outgoing_ != static_cast<mqd_t>(-1) && outgoing_ != incoming_) {
        mq_close(outgoing_);
    }
    if (role_ == Role::Host) {
        mq_unlink(h2c_name_.c_str());
        mq_unlink(c2h_name_.c_str());
    }
}

void Conn::SetError(const std::string& message) {
    failed_ = true;
    error_ = message;
    std::cerr << "[mq] " << message << '\n';
}
