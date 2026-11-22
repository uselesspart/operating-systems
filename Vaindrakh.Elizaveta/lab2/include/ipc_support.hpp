#pragma once

#include "connection.hpp"
#include <semaphore.h>
#include <sys/types.h>
#include <string>

namespace lab {
[[noreturn]] void systemError(const std::string& operation);
void waitSemaphore(sem_t& semaphore);
void postSemaphore(sem_t& semaphore);
void* mapAnonymous(std::size_t bytes);

// Unnamed, process-shared semaphores inherited through anonymous mmap.
class Sync {
public:
    Sync();
    ~Sync();
    Sync(const Sync&) = delete;
    Sync& operator=(const Sync&) = delete;
    sem_t& incoming(Role role);
    sem_t& outgoing(Role role);
    sem_t& bootstrap();
private:
    struct Area { sem_t host; sem_t client; sem_t ready; };
    Area* area_;
    pid_t owner_;
};
}
