#pragma once

#include "session.h"

class Conn {
public:
    bool open(const Session &session, bool create);
    bool read(int *value);
    bool write(int value);
    ~Conn();

private:
    int fd = -1;
    char path[128] = {};
    bool owner = false;
};
