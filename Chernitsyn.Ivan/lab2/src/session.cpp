#include "session.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static const char *const suffix[] = {"request", "reply", "slot"};

bool Session::open(pid_t host, bool create)
{
    snprintf(prefix, sizeof(prefix), "/goat_%u_%d", getuid(), host);
    snprintf(directory, sizeof(directory), "/tmp%s", prefix);
    if (create) {
        if (mkdir(directory, 0700) < 0)
            return false;
        owner = true;
    }

    sem_t **sems[] = {&request, &reply, &slot};
    for (unsigned i = 0; i < 3; ++i) {
        char name[96];
        snprintf(name, sizeof(name), "%s_%s", prefix, suffix[i]);
        *sems[i] = sem_open(name, create ? O_CREAT | O_EXCL : 0, 0600, i == 2);
        if (*sems[i] == SEM_FAILED)
            return false;
        if (create)
            ++created;
    }
    return true;
}

bool Session::claim()
{
    return sem_trywait(slot) == 0;
}

Session::~Session()
{
    sem_t *sems[] = {request, reply, slot};
    for (unsigned i = 0; i < 3; ++i) {
        if (sems[i] != SEM_FAILED)
            sem_close(sems[i]);
        if (i < created) {
            char name[96];
            snprintf(name, sizeof(name), "%s_%s", prefix, suffix[i]);
            sem_unlink(name);
        }
    }
    if (owner)
        rmdir(directory);
}

bool wait_five_seconds(sem_t *sem)
{
    timespec until;
    if (clock_gettime(CLOCK_REALTIME, &until) < 0)
        return false;
    until.tv_sec += 5;
    while (sem_timedwait(sem, &until) < 0) {
        if (errno != EINTR)
            return false;
    }
    return true;
}
