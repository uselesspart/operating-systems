#include "conn_fifo.hpp"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace {
int OpenFifoWithRetry(const std::string& path) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        const int fd = open(path.c_str(), O_RDWR | O_NONBLOCK);
        if (fd != -1) {
            return fd;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return -1;
}
}

Conn::Conn(Role role, pid_t host_pid, SharedSync* sync)
    : role_(role),
      host_pid_(host_pid),
      sync_(sync),
      incoming_fd_(-1),
      outgoing_fd_(-1),
      h2c_path_("/tmp/os_lab2_v13_" + std::to_string(host_pid_) + "_h2c.fifo"),
      c2h_path_("/tmp/os_lab2_v13_" + std::to_string(host_pid_) + "_c2h.fifo"),
      failed_(false) {
    if (role_ == Role::Host) {
        unlink(h2c_path_.c_str());
        unlink(c2h_path_.c_str());
        if (mkfifo(h2c_path_.c_str(), 0600) == -1 || mkfifo(c2h_path_.c_str(), 0600) == -1) {
            SetError(std::string("mkfifo: ") + std::strerror(errno));
            return;
        }
    }

    const int h2c = OpenFifoWithRetry(h2c_path_);
    const int c2h = OpenFifoWithRetry(c2h_path_);
    if (h2c == -1 || c2h == -1) {
        if (h2c != -1) {
            close(h2c);
        }
        if (c2h != -1) {
            close(c2h);
        }
        SetError("FIFO endpoints were not available within 5 seconds");
        return;
    }

    if (role_ == Role::Host) {
        outgoing_fd_ = h2c;
        incoming_fd_ = c2h;
    } else {
        incoming_fd_ = h2c;
        outgoing_fd_ = c2h;
    }
}

bool Conn::Read(void* buf, std::size_t count) {
    if (failed_ || incoming_fd_ == -1) {
        return false;
    }

    if (!TimedLock(&sync_->io_lock)) {
        SetError("local semaphore timeout in Read");
        return false;
    }

    char temp[1024];
    const ssize_t n = read(incoming_fd_, temp, sizeof(temp));
    const int saved_errno = errno;
    Unlock(&sync_->io_lock);

    if (n > 0) {
        read_buffer_.insert(read_buffer_.end(), temp, temp + n);
    } else if (n == -1 && saved_errno != EAGAIN && saved_errno != EWOULDBLOCK) {
        SetError(std::string("read FIFO: ") + std::strerror(saved_errno));
        return false;
    }

    if (read_buffer_.size() < count) {
        return false;
    }

    std::memcpy(buf, read_buffer_.data(), count);
    read_buffer_.erase(read_buffer_.begin(), read_buffer_.begin() + static_cast<std::ptrdiff_t>(count));
    return true;
}

bool Conn::Write(const void* buf, std::size_t count) {
    if (failed_ || outgoing_fd_ == -1) {
        return false;
    }
    if (!TimedLock(&sync_->io_lock)) {
        SetError("local semaphore timeout in Write");
        return false;
    }
    const ssize_t n = write(outgoing_fd_, buf, count);
    const int saved_errno = errno;
    Unlock(&sync_->io_lock);

    if (n == static_cast<ssize_t>(count)) {
        return true;
    }
    if (n == -1) {
        SetError(std::string("write FIFO: ") + std::strerror(saved_errno));
    } else {
        SetError("partial FIFO write");
    }
    return false;
}

bool Conn::Failed() const {
    return failed_;
}

std::string Conn::Error() const {
    return error_;
}

Conn::~Conn() {
    if (incoming_fd_ != -1) {
        close(incoming_fd_);
    }
    if (outgoing_fd_ != -1 && outgoing_fd_ != incoming_fd_) {
        close(outgoing_fd_);
    }
    if (role_ == Role::Host) {
        unlink(h2c_path_.c_str());
        unlink(c2h_path_.c_str());
    }
}

void Conn::SetError(const std::string& message) {
    failed_ = true;
    error_ = message;
    std::cerr << "[fifo] " << message << '\n';
}
