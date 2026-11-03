#include "test_sems.h"
#include "SharedSems.h"
#include <cassert>
#include <sys/wait.h>
#include <unistd.h>

void testSems() {
    SharedSems* sems = createSharedSems();

    // таймаут, никто не постит
    assert(!semWaitTimeout(&sems->host_msg_ready, 1));

    // post из другого процесса
    pid_t pid = fork();
    if (pid == 0) {
        sem_post(&sems->host_msg_ready);
        _exit(0);
    }
    assert(semWaitTimeout(&sems->host_msg_ready, 5));
    waitpid(pid, nullptr, 0);

    // повторный post из ребёнка для второго семафора
    pid = fork();
    if (pid == 0) {
        sem_post(&sems->client_msg_ready);
        _exit(0);
    }
    assert(semWaitTimeout(&sems->client_msg_ready, 5));
    waitpid(pid, nullptr, 0);
}