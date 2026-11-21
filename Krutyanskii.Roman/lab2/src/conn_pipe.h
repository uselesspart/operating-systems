#pragma once
#include "common.h"

class ConnPipe : public Conn {
    int pipefd[2] = {-1,-1};
public:
    ConnPipe();
    ~ConnPipe();
    bool Read(void* buf, size_t count) override;
    bool Write(const void* buf, size_t count) override;
    int getReadFd() const { return pipefd[0]; }
    int getWriteFd() const { return pipefd[1]; }
};
