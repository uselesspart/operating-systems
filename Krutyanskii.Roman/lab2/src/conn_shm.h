#pragma once
#include "common.h"
#include <sys/mman.h>
#include <fcntl.h>

class ConnShm : public Conn {
    int fd = -1;
    void* addr = nullptr;
    size_t sz = 4096;
public:
    ConnShm();
    ~ConnShm();
    bool Read(void* buf, size_t count) override;
    bool Write(const void* buf, size_t count) override;
};
