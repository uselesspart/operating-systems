#pragma once
#include <unistd.h>

namespace chat {
class FileDescriptor final {
public:
    explicit FileDescriptor(int fd = -1) : fd_(fd) {}
    ~FileDescriptor() { reset(); }
    FileDescriptor(const FileDescriptor&) = delete;
    FileDescriptor& operator=(const FileDescriptor&) = delete;
    int get() const { return fd_; }
    void reset(int fd = -1) { if (fd_ >= 0) close(fd_); fd_ = fd; }
private:
    int fd_;
};
}
