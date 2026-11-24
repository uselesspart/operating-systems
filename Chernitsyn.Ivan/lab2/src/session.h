#pragma once

#include <semaphore.h>
#include <sys/types.h>

class Session {
public:
    Session() = default;
    Session(const Session &) = delete;
    bool open(pid_t host, bool create);
    bool claim();
    ~Session();

    char directory[96] = {};
    char prefix[64] = {};
    sem_t *request = SEM_FAILED;
    sem_t *reply = SEM_FAILED;

private:
    sem_t *slot = SEM_FAILED;
    bool owner = false;
    unsigned created = 0;
};

bool wait_five_seconds(sem_t *sem);
