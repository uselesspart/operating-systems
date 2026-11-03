#include "ConnFifo.h"
#include "Protocol.h"
#include "SharedSems.h"
#include <cassert>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

// Аналог test_conn_mq, но обмен идёт через два именованных канала(h2c и c2h). Проверяет, что mkfifo + open + read/write работают и что три сообщения проходят в правильном порядке
int main() {
    SharedSems* sems = createSharedSems();
    ConnFifo::preFork(sems);

    pid_t pid = fork();
    if (pid < 0) { std::perror("fork"); return 1; }

    if (pid == 0) {
        auto conn = ConnFifo::createClient(sems);

        assert(semWaitTimeout(&sems->host_msg_ready, 5));
        HostToClient wolf{};
        assert(conn->Read(&wolf, sizeof(wolf)));
        assert(wolf.type == WolfNumber);
        assert(wolf.value == 42);

        ClientToHost kid{};
        kid.type = KidNumber;
        kid.value = 17;
        assert(conn->Write(&kid, sizeof(kid)));
        sem_post(&sems->client_msg_ready);

        assert(semWaitTimeout(&sems->host_msg_ready, 5));
        HostToClient status{};
        assert(conn->Read(&status, sizeof(status)));
        assert(status.type == Status);
        assert(status.value == 1);

        _exit(0);
    }

    auto conn = ConnFifo::createHost(sems);

    HostToClient wolf{};
    wolf.type = WolfNumber;
    wolf.value = 42;
    assert(conn->Write(&wolf, sizeof(wolf)));
    sem_post(&sems->host_msg_ready);

    assert(semWaitTimeout(&sems->client_msg_ready, 5));
    ClientToHost kid{};
    assert(conn->Read(&kid, sizeof(kid)));
    assert(kid.type == KidNumber);
    assert(kid.value == 17);

    HostToClient status{};
    status.type = Status;
    status.value = 1;
    assert(conn->Write(&status, sizeof(status)));
    sem_post(&sems->host_msg_ready);

    int rc = 0;
    waitpid(pid, &rc, 0);
    assert(WIFEXITED(rc) && WEXITSTATUS(rc) == 0);

    std::cout << "test_conn_fifo: PASSED\n";
    return 0;
}