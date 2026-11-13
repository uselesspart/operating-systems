#pragma once

#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <unistd.h>

namespace reminder {
// Owns one descriptor; copying would incorrectly create two owners.
class FileDescriptor {
public:
    explicit FileDescriptor(int value) : value_(value) {
        if (value < 0) throw std::runtime_error(std::strerror(errno));
    }
    ~FileDescriptor() { if (value_ >= 0) close(value_); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    int get() const { return value_; }
    void reset() {
        if (value_ >= 0) close(value_);
        value_ = -1;
    }
private:
    int value_;
};

}
