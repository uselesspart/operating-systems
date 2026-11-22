#include "ipc_support.hpp"
#include <cerrno>
#include <cstring>
#include <ctime>
#include <sys/mman.h>
#include <unistd.h>

namespace lab {
void systemError(const std::string& operation) {
    const int code = errno;
    throw std::runtime_error(operation + ": " + std::strerror(code));
}
void* mapAnonymous(std::size_t bytes) {
    void* result = mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (result == MAP_FAILED) systemError("mmap anonymous");
    return result;
}
void waitSemaphore(sem_t& semaphore) {
    timespec deadline{};
    if (clock_gettime(CLOCK_REALTIME, &deadline) == -1) systemError("clock_gettime");
    deadline.tv_sec += timeoutSeconds;
    while (sem_timedwait(&semaphore, &deadline) == -1) {
        if (errno == EINTR) continue; // Do not restart the five-second deadline.
        if (errno == ETIMEDOUT) throw std::runtime_error("IPC timeout after 5 seconds");
        systemError("sem_timedwait");
    }
}
void postSemaphore(sem_t& semaphore) {
    if (sem_post(&semaphore) == -1) systemError("sem_post");
}
Sync::Sync() : area_(static_cast<Area*>(mapAnonymous(sizeof(Area)))), owner_(getpid()) {
    sem_t* items[] = {&area_->host, &area_->client, &area_->ready};
    int initialized = 0;
    try {
        for (auto* item : items) {
            if (sem_init(item, 1, 0) == -1) systemError("sem_init");
            ++initialized;
        }
    } catch (...) {
        for (int i = 0; i < initialized; ++i) sem_destroy(items[i]);
        munmap(area_, sizeof(Area));
        throw;
    }
}
Sync::~Sync() {
    // The owner must reap its child before destroying these semaphores.
    if (getpid() == owner_) {
        sem_destroy(&area_->host);
        sem_destroy(&area_->client);
        sem_destroy(&area_->ready);
    }
    munmap(area_, sizeof(Area));
}
sem_t& Sync::incoming(Role role) { return role == Role::Host ? area_->host : area_->client; }
sem_t& Sync::outgoing(Role role) { return role == Role::Host ? area_->client : area_->host; }
sem_t& Sync::bootstrap() { return area_->ready; }
}
