#pragma once
#include <cstddef>

class Conn {
public:
    virtual ~Conn() = default;
    virtual bool Write(const void* buf, size_t count) = 0;
    virtual bool Read(void* buf, size_t count)  = 0;
};