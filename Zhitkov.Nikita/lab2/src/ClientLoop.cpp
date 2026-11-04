#include "ClientLoop.h"
#include "Protocol.h"

#include <cstdlib>
#include <ctime>
#include <unistd.h>
#include <syslog.h>

void runClientLoop(Conn* conn, SharedSems* sems) {
    openlog("lab2_client", LOG_PID, LOG_USER);
    std::srand(std::time(nullptr) ^ getpid());

    // Статус клиента на начало - родился живым. После каждого раунда обновляется из сообщения Status
    int32_t kid_alive = 1;

    for (;;) {
        if (!semWaitTimeout(&sems->host_msg_ready, 5)) {
            syslog(LOG_WARNING, "timeout waiting for host message");
            break;
        }

        HostToClient msg{};
        if (!conn->Read(&msg, sizeof(msg))) {
            syslog(LOG_ERR, "read from host failed");
            break;
        }

        if (msg.type == WolfNumber) {
            ClientToHost reply{};
            reply.type  = KidNumber;
            reply.value = 1 + std::rand() % (kid_alive ? 100 : 50);
            if (!conn->Write(&reply, sizeof(reply))) {
                syslog(LOG_ERR, "write kid number to host failed");
                break;
            }
            sem_post(&sems->client_msg_ready);
            syslog(LOG_INFO, "round: kid_num=%d (alive=%d)",
                   reply.value, kid_alive);
        } else if (msg.type == Status) {
            kid_alive = msg.value;
            syslog(LOG_INFO, "round result: kid_alive=%d", kid_alive);
        } else {
            syslog(LOG_ERR, "unknown message type %d, desync", msg.type);
            break;
        }
    }

    syslog(LOG_INFO, "client loop finished");
    closelog();
}