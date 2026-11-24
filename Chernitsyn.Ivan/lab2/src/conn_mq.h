#pragma once

#include "session.h"

#include <mqueue.h>

class Conn {
public:
    bool open(const Session &session, bool create);
    bool read(int *value);
    bool write(int value);
    ~Conn();

private:
    mqd_t queue = -1;
    char name[96] = {};
    bool owner = false;
};
