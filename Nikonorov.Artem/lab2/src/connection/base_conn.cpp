#include "conn.hpp"

#include <stdexcept>

Conn::Conn(ConnRole role, pid_t host_pid, SharedSync* sync)
    : role_(role), host_pid_(host_pid), sync_(sync) {
    if (sync_ == nullptr) {
        throw std::invalid_argument("Shared synchronization memory is null");
    }
}

void Conn::Cancel() noexcept {
    if (cancelled_.exchange(true)) {
        return;
    }

    sem_t* semaphore = role_ == ConnRole::Host
        ? &sync_->client_to_host
        : &sync_->host_to_client;
    sem_post(semaphore);
}

const std::string& Conn::LastError() const noexcept {
    return error_;
}

bool Conn::WaitReadTurn() {
    if (cancelled_.load()) {
        return Fail("Connection was cancelled");
    }
    sem_t* semaphore = role_ == ConnRole::Host
        ? &sync_->client_to_host
        : &sync_->host_to_client;
    if (!WaitSemaphore(semaphore, kTimeoutSeconds, error_)) {
        return false;
    }
    if (cancelled_.load()) {
        return Fail("Connection was cancelled");
    }
    return true;
}

bool Conn::PostWriteTurn() {
    if (cancelled_.load()) {
        return Fail("Connection was cancelled");
    }
    sem_t* semaphore = role_ == ConnRole::Host
        ? &sync_->host_to_client
        : &sync_->client_to_host;
    return PostSemaphore(semaphore, error_);
}

bool Conn::Fail(const std::string& message) {
    error_ = message;
    return false;
}
