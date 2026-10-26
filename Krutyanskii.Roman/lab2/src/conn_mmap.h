#pragma once
#include "common.h"
#include <sys/mman.h>

class ConnMmap : public Conn {
    void* addr = nullptr;
    size_t sz = 4096;
public:
    ConnMmap();
    ~ConnMmap();
    bool Read(void* buf, size_t count) override;
    bool Write(const void* buf, size_t count) override;
};
