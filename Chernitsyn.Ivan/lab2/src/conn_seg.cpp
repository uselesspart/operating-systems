#include "conn_seg.h"

#include <sys/shm.h>

bool Conn::open(const Session &session, bool create)
{
    key_t key = ftok(session.directory, 'G');
    if (key == -1)
        return false;
    id = shmget(key, sizeof(int), create ? IPC_CREAT | IPC_EXCL | 0600 : 0600);
    if (id < 0)
        return false;
    owner = create;
    void *addr = shmat(id, nullptr, 0);
    if (addr == (void *)-1)
        return false;
    data = (int *)addr;
    return true;
}

bool Conn::read(int *value)
{
    *value = *data;
    return true;
}

bool Conn::write(int value)
{
    *data = value;
    return true;
}

Conn::~Conn()
{
    if (data)
        shmdt(data);
    if (owner)
        shmctl(id, IPC_RMID, nullptr);
}
