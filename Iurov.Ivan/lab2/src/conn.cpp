#include "wolf/conn.hpp"

#include <stdexcept>

namespace wolf {
namespace {

sem_t* Incoming(Role role, SharedState* state) {
    return role == Role::Host ? &state->client_turn : &state->host_turn;
}

sem_t* Outgoing(Role role, SharedState* state) {
    return role == Role::Host ? &state->host_turn : &state->client_turn;
}

}

Conn::Conn(Role role, SharedState* state) : role_(role), state_(state) {
    if (state_ == nullptr) {
        throw std::invalid_argument("состояние соединения не задано");
    }
}

void Conn::Cancel() noexcept {
    if (cancelled_.exchange(true)) {
        return;
    }
    std::string error;
    PostSemaphore(Incoming(role_, state_), error);
}

bool Conn::WaitTurn() {
    if (cancelled_.load()) {
        return Fail("соединение отменено");
    }
    std::string error;
    if (!WaitSemaphore(Incoming(role_, state_), kIpcTimeoutSeconds, error)) {
        return Fail(error);
    }
    if (cancelled_.load()) {
        return Fail("соединение отменено");
    }
    return true;
}

bool Conn::PostTurn() {
    if (cancelled_.load()) {
        return Fail("соединение отменено");
    }
    std::string error;
    if (!PostSemaphore(Outgoing(role_, state_), error)) {
        return Fail(error);
    }
    return true;
}

bool Conn::Fail(const std::string& message) {
    error_ = message;
    return false;
}

}
