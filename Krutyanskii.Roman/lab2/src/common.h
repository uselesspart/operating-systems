#pragma once
#include <cstddef>
#include <string>

class Conn {
public:
    virtual bool Read(void* buf, size_t count) = 0;
    virtual bool Write(const void* buf, size_t count) = 0;
    virtual ~Conn() = default;
};

void logMsg(const std::string& s);

// Wolf & Kids state transition for a single kid (n = 1).
// A living kid hides (stays alive) when |wolf - kid| <= 70/n, otherwise it is
// caught. A dead kid revives when |wolf - kid| <= 20/n.
bool kidNextAlive(bool wasAlive, int wolf, int kid);
