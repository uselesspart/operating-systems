#pragma once

#include <semaphore.h>
#include <string>

namespace wolf {

inline constexpr int kIpcTimeoutSeconds = 5;

struct SharedState {
    sem_t host_turn;
    sem_t client_turn;
    sem_t channel_ready;
    char channel_name[128];
    int socket_fds[2];
};

class SharedStateArea {
public:
    SharedStateArea();
    ~SharedStateArea();

    SharedStateArea(const SharedStateArea&) = delete;
    SharedStateArea& operator=(const SharedStateArea&) = delete;

    SharedState* Get() const noexcept { return state_; }

private:
    SharedState* state_ = nullptr;
    bool host_turn_ready_ = false;
    bool client_turn_ready_ = false;
    bool channel_ready_ready_ = false;
};

bool WaitSemaphore(sem_t* semaphore, int seconds, std::string& error);
bool PostSemaphore(sem_t* semaphore, std::string& error);

}
