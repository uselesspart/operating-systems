#pragma once

#include "session.h"

class Conn {
public:
    bool open(const Session &session, bool create);
    bool read(int *value);
    bool write(int value);
    ~Conn();

private:
    int id = -1;
    int *data = nullptr;
    bool owner = false;
};
