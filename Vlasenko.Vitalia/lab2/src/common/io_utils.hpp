#pragma once

#include "common/deadline.hpp"

#include <cstddef>

namespace lab2::common {

void setNonBlocking(int fd);
void setCloseOnExec(int fd);

// Для pipe, FIFO и потоковых сокетов с O_NONBLOCK.
// После ошибки или тайм-аута часть сообщения может быть уже передана: соединение закрывается.
void readExact(int fd, void* buffer, std::size_t count, const Deadline& deadline = Deadline{});
void writeExact(int fd, const void* buffer, std::size_t count,
                const Deadline& deadline = Deadline{});

} // namespace lab2::common
