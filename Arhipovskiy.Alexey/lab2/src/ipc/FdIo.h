#ifndef CHAT_FD_IO_H
#define CHAT_FD_IO_H

#include <chrono>
#include <cstddef>

namespace chat::fdio {

// Both return false on end of stream or error; EINTR and short transfers are handled
bool readExact(int fd, void* buffer, std::size_t count);
bool writeExact(int fd, const void* buffer, std::size_t count);

// Socket variant of writeExact that never raises SIGPIPE
bool sendExact(int fd, const void* buffer, std::size_t count);

bool waitReadable(int fd, std::chrono::milliseconds timeout);
void setBlocking(int fd, bool blocking);

}

#endif // CHAT_FD_IO_H
