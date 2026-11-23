#pragma once

#include <chrono>
#include <cstddef>

namespace chatlab::fdops {

bool readFull(int fd, void* data, std::size_t size);
bool writeFull(int fd, const void* data, std::size_t size);
bool sendFull(int fd, const void* data, std::size_t size);

bool waitReadable(int fd, std::chrono::milliseconds timeout);
void setBlocking(int fd);

}
