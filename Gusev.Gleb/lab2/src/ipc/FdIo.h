#pragma once

#include <chrono>
#include <cstddef>
#include <vector>

// Чтение и запись ровно N байт с тайм-аутом поверх poll(); подходит для pipe,
// FIFO и сокета, включая неблокирующие дескрипторы.
namespace wolf::fdio {

bool waitReadable(int fd, std::chrono::milliseconds timeout);
bool readExact(int fd, void* buf, std::size_t count, std::chrono::milliseconds timeout);
bool writeExact(int fd, const void* buf, std::size_t count, std::chrono::milliseconds timeout);
bool setNonBlocking(int fd);
void closeAllExcept(const std::vector<int>& keep);

}
