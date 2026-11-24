#include "conn_mq.h"

#include <stdio.h>

bool Conn::open(const Session &session, bool create)
{
    snprintf(name, sizeof(name), "%s_mq", session.prefix);
    mq_attr attr = {};
    attr.mq_maxmsg = 1;
    attr.mq_msgsize = sizeof(int);
    int flags = O_RDWR | O_NONBLOCK;
    if (create)
        flags |= O_CREAT | O_EXCL;
    queue = mq_open(name, flags, 0600, &attr);
    owner = create && queue != -1;
    return queue != -1;
}

bool Conn::read(int *value)
{
    return mq_receive(queue, (char *)value, sizeof(*value), nullptr) == sizeof(*value);
}

bool Conn::write(int value)
{
    return mq_send(queue, (const char *)&value, sizeof(value), 0) == 0;
}

Conn::~Conn()
{
    if (queue != -1)
        mq_close(queue);
    if (owner)
        mq_unlink(name);
}
