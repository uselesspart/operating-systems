#pragma once

#include <semaphore.h>

#include <string>

struct SharedSync {
    sem_t host_to_client;
    sem_t client_to_host;
};

class SharedSyncMapping {
public:
    SharedSyncMapping();
    ~SharedSyncMapping();

    SharedSyncMapping(const SharedSyncMapping&) = delete;
    SharedSyncMapping& operator=(const SharedSyncMapping&) = delete;

    SharedSync* get() const noexcept;

private:
    SharedSync* sync_;
    bool host_to_client_initialized_;
    bool client_to_host_initialized_;
};

bool WaitSemaphore(sem_t* semaphore, int timeout_seconds, std::string& error);
bool PostSemaphore(sem_t* semaphore, std::string& error);
