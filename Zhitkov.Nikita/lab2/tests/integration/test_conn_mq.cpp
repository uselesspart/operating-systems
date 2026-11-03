#include "ConnMq.h"
#include "Protocol.h"
#include "SharedSems.h"
#include <cassert>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

// Проверяет реальный обмен через очередь сообщений:
// host -> client (WolfNumber), client → host (KidNumber)
// host -> client (Status). Все три сообщения - через mq_send/mq_receive
int main() {
    SharedSems* sems = createSharedSems();
    ConnMq::preFork(sems);

    pid_t pid = fork();
    if (pid < 0) { std::perror("fork"); return 1; }

    if (pid == 0) {
        auto conn = ConnMq::createClient(sems);

        // 1. Принимаем число волка
        assert(semWaitTimeout(&sems->host_msg_ready, 5));
        HostToClient wolf{};
        assert(conn->Read(&wolf, sizeof(wolf)));
        assert(wolf.type == WolfNumber);
        assert(wolf.value == 42);

        // 2. Отправляем число козлёнка
        ClientToHost kid{};
        kid.type = KidNumber;
        kid.value = 17;
        assert(conn->Write(&kid, sizeof(kid)));
        sem_post(&sems->client_msg_ready);

        // 3. Принимаем статус после раунда
        assert(semWaitTimeout(&sems->host_msg_ready, 5));
        HostToClient status{};
        assert(conn->Read(&status, sizeof(status)));
        assert(status.type == Status);
        assert(status.value == 1);

        _exit(0);
    }

    auto conn = ConnMq::createHost(sems);

    // 1. Отправляем число волка
    HostToClient wolf{};
    wolf.type = WolfNumber;
    wolf.value = 42;
    assert(conn->Write(&wolf, sizeof(wolf)));
    sem_post(&sems->host_msg_ready);

    // 2. Принимаем число козлёнка
    assert(semWaitTimeout(&sems->client_msg_ready, 5));
    ClientToHost kid{};
    assert(conn->Read(&kid, sizeof(kid)));
    assert(kid.type == KidNumber);
    assert(kid.value == 17);

    // 3. Отправляем статус
    HostToClient status{};
    status.type = Status;
    status.value = 1;
    assert(conn->Write(&status, sizeof(status)));
    sem_post(&sems->host_msg_ready);

    int rc = 0;
    waitpid(pid, &rc, 0);
    assert(WIFEXITED(rc) && WEXITSTATUS(rc) == 0);

    std::cout << "test_conn_mq: PASSED\n";
    return 0;
}