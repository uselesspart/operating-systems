#pragma once

#include <cstddef>
#include <string>

class Conn {
public:
    Conn(int id, bool create);
    ~Conn();

    Conn(const Conn&) = delete;
    Conn& operator=(const Conn&) = delete;

    bool Read(void* buf, size_t count);
    bool Write(const void* buf, size_t count);

    static const char* Type();

private:
    void closeAll();

    int id_;
    bool create_;
    int rd_ = -1;
    int wr_ = -1;
};
