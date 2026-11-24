#include "conn_fifo.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

bool Conn::open(const Session &session, bool create)
{
    snprintf(path, sizeof(path), "%s/fifo", session.directory);
    if (create && mkfifo(path, 0600) < 0)
        return false;
    owner = create;
    fd = ::open(path, O_RDWR | O_NONBLOCK);
    return fd >= 0;
}

bool Conn::read(int *value)
{
    return ::read(fd, value, sizeof(*value)) == sizeof(*value);
}

bool Conn::write(int value)
{
    return ::write(fd, &value, sizeof(value)) == sizeof(value);
}

Conn::~Conn()
{
    if (fd >= 0)
        close(fd);
    if (owner)
        unlink(path);
}
