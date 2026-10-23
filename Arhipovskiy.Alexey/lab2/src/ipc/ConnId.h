#ifndef CHAT_CONN_ID_H
#define CHAT_CONN_ID_H

#include <sys/types.h>

namespace chat {

// A channel is identified by the host process and the client number it gave out at handshake
struct ConnId {
    pid_t hostPid;
    int clientId;
};

}

#endif // CHAT_CONN_ID_H
