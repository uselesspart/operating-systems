#include "handshake/handshake_signals.hpp"

#include <pthread.h>
#include <stdexcept>
#include <system_error>

namespace lab2::handshake {

void blockHandshakeSignals() {
    sigset_t signals{};
    sigemptyset(&signals);
    sigaddset(&signals, SIGUSR1);
    sigaddset(&signals, SIGUSR2);
    const int error = pthread_sigmask(SIG_BLOCK, &signals, nullptr);
    if (error != 0) {
        throw std::system_error(error, std::generic_category(), "pthread_sigmask");
    }
}

void requireHandshakeSignalsBlocked() {
    sigset_t current{};
    const int error = pthread_sigmask(SIG_SETMASK, nullptr, &current);
    if (error != 0) {
        throw std::system_error(error, std::generic_category(), "pthread_sigmask");
    }
    if (sigismember(&current, SIGUSR1) != 1 || sigismember(&current, SIGUSR2) != 1) {
        throw std::logic_error("Handshake signals must be blocked before starting threads");
    }
}

} // namespace lab2::handshake
