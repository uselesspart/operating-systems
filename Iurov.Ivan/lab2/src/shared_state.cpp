#include "wolf/shared_state.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <sys/mman.h>

namespace wolf {

SharedStateArea::SharedStateArea() {
    void* memory = mmap(nullptr, sizeof(SharedState), PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) {
        throw std::runtime_error("mmap: " + std::string(std::strerror(errno)));
    }

    state_ = static_cast<SharedState*>(memory);
    std::memset(state_, 0, sizeof(SharedState));
    state_->socket_fds[0] = -1;
    state_->socket_fds[1] = -1;
    state_->channel_name[0] = '\0';

    if (sem_init(&state_->host_turn, 1, 0) != 0) {
        const std::string error = std::strerror(errno);
        munmap(state_, sizeof(SharedState));
        state_ = nullptr;
        throw std::runtime_error("sem_init(host_turn): " + error);
    }
    host_turn_ready_ = true;

    if (sem_init(&state_->client_turn, 1, 0) != 0) {
        const std::string error = std::strerror(errno);
        sem_destroy(&state_->host_turn);
        host_turn_ready_ = false;
        munmap(state_, sizeof(SharedState));
        state_ = nullptr;
        throw std::runtime_error("sem_init(client_turn): " + error);
    }
    client_turn_ready_ = true;

    if (sem_init(&state_->channel_ready, 1, 0) != 0) {
        const std::string error = std::strerror(errno);
        sem_destroy(&state_->client_turn);
        sem_destroy(&state_->host_turn);
        client_turn_ready_ = false;
        host_turn_ready_ = false;
        munmap(state_, sizeof(SharedState));
        state_ = nullptr;
        throw std::runtime_error("sem_init(channel_ready): " + error);
    }
    channel_ready_ready_ = true;
}

SharedStateArea::~SharedStateArea() {
    if (state_ == nullptr) {
        return;
    }
    if (channel_ready_ready_) {
        sem_destroy(&state_->channel_ready);
    }
    if (client_turn_ready_) {
        sem_destroy(&state_->client_turn);
    }
    if (host_turn_ready_) {
        sem_destroy(&state_->host_turn);
    }
    munmap(state_, sizeof(SharedState));
}

bool WaitSemaphore(sem_t* semaphore, int seconds, std::string& error) {
    timespec deadline{};
    if (clock_gettime(CLOCK_REALTIME, &deadline) != 0) {
        error = "clock_gettime: " + std::string(std::strerror(errno));
        return false;
    }
    deadline.tv_sec += seconds;

    while (sem_timedwait(semaphore, &deadline) != 0) {
        if (errno == EINTR) {
            continue;
        }
        if (errno == ETIMEDOUT) {
            error = "ожидание семафора: таймаут " + std::to_string(seconds) + " с";
        } else {
            error = "sem_timedwait: " + std::string(std::strerror(errno));
        }
        return false;
    }
    return true;
}

bool PostSemaphore(sem_t* semaphore, std::string& error) {
    if (sem_post(semaphore) != 0) {
        error = "sem_post: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

}
