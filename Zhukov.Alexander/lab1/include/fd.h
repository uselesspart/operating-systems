#pragma once

#include <cerrno>
#include <system_error>
#include <utility>
#include <fcntl.h>
#include <unistd.h>

class Fd {
public:
    explicit Fd(int fd = -1) : fd(fd) {}
    ~Fd() { reset(); }

    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;

    int get() const { return fd; }
    int release() { return std::exchange(fd, -1); }

    void reset(int value = -1) {
        if (fd >= 0) {
            close(fd);
        }
        fd = value;
    }

    void move_up() {
        if (fd >= 0 && fd < 3) {
            const int value = fcntl(fd, F_DUPFD_CLOEXEC, 3);
            if (value < 0) {
                throw std::system_error(errno, std::generic_category(), "Cannot copy descriptor");
            }
            reset(value);
        }
    }

private:
    int fd;
};
