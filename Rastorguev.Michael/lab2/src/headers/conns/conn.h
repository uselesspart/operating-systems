#pragma once
#include <cstddef>
#include <sys/types.h>


class Conn {
public:

    Conn(pid_t id, bool create);
    ~Conn();

    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    bool Ok() const;
    bool Read(void* buf, size_t count);
    bool Write(const void* buf, size_t count);

    static const bool kCreateBeforeFork;

private:
    struct Impl;
    Impl* impl_;
};
