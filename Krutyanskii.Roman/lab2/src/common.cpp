#include "common.h"
#include <cstdlib>
#include <iostream>
void logMsg(const std::string& s) { std::cout << "[LOG] " << s << std::endl; }

bool kidNextAlive(bool wasAlive, int wolf, int kid) {
    int diff = std::abs(wolf - kid);
    return wasAlive ? (diff <= 70) : (diff <= 20);
}
