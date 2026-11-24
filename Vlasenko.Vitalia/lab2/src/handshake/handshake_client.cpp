#include "handshake/handshake_client.hpp"

#include "common/deadline.hpp"
#include "handshake/handshake_signals.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace lab2::handshake {
namespace {

constexpr int kRetryMilliseconds = 100;

} // namespace

std::uint32_t HandshakeClient::connect(pid_t hostPid, std::chrono::milliseconds timeout) const {
    if (hostPid <= 0 || hostPid == getpid()) {
        throw std::invalid_argument("Invalid host PID");
    }
    requireHandshakeSignalsBlocked();
    const common::Deadline deadline(timeout);
    sigset_t replies{};
    sigemptyset(&replies);
    sigaddset(&replies, SIGUSR2);

    // Обычные сигналы не образуют очередь. Повторяем запрос, не продлевая общий срок.
    while (deadline.remainingMilliseconds() > 0) {
        if (kill(hostPid, SIGUSR1) == -1) {
            throw std::system_error(errno, std::generic_category(), "kill(SIGUSR1)");
        }
        const common::Deadline retry(std::chrono::milliseconds{
            std::min(kRetryMilliseconds, deadline.remainingMilliseconds())});
        while (retry.remainingMilliseconds() > 0 && deadline.remainingMilliseconds() > 0) {
            const int remaining =
                std::min(retry.remainingMilliseconds(), deadline.remainingMilliseconds());
            const timespec wait{remaining / 1000, (remaining % 1000) * 1000000L};
            siginfo_t info{};
            const int signal = sigtimedwait(&replies, &info, &wait);
            if (signal == SIGUSR2) {
                if (info.si_pid == hostPid && info.si_code == SI_QUEUE &&
                    info.si_value.sival_int > 0) {
                    return static_cast<std::uint32_t>(info.si_value.sival_int);
                }
                continue;
            }
            if (errno == EAGAIN) {
                break;
            }
            if (errno != EINTR) {
                throw std::system_error(errno, std::generic_category(), "sigtimedwait(SIGUSR2)");
            }
        }
    }
    throw std::system_error(ETIMEDOUT, std::generic_category(), "Handshake timed out");
}

} // namespace lab2::handshake
