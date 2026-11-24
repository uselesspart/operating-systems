#include "handshake/handshake_server.hpp"

#include "common/deadline.hpp"
#include "handshake/handshake_signals.hpp"

#include <cerrno>
#include <climits>
#include <csignal>
#include <stdexcept>
#include <system_error>
#include <unistd.h>

namespace lab2::handshake {

std::optional<HandshakeResult> HandshakeServer::handleNext(const PrepareSession& prepare,
                                                           std::chrono::milliseconds timeout) {
    if (!prepare) {
        throw std::invalid_argument("Missing session preparation callback");
    }
    requireHandshakeSignalsBlocked();
    const common::Deadline deadline(timeout);
    sigset_t requests{};
    sigemptyset(&requests);
    sigaddset(&requests, SIGUSR1);

    for (;;) {
        const int remaining = deadline.remainingMilliseconds();
        const timespec wait{remaining / 1000, (remaining % 1000) * 1000000L};
        siginfo_t info{};
        const int signal = sigtimedwait(&requests, &info, &wait);
        if (signal == -1) {
            if (errno == EAGAIN) {
                return std::nullopt;
            }
            if (errno != EINTR) {
                throw std::system_error(errno, std::generic_category(), "sigtimedwait(SIGUSR1)");
            }
        } else if (info.si_pid > 0 && info.si_pid != getpid() &&
                   (info.si_code == SI_USER || info.si_code == SI_QUEUE)) {
            const pid_t pid = info.si_pid;
            const auto [entry, inserted] = sessions_.try_emplace(pid, 0);
            if (inserted) {
                try {
                    const auto id = prepare(pid);
                    if (id == 0 || id > static_cast<std::uint32_t>(INT_MAX)) {
                        throw std::invalid_argument("Invalid session ID");
                    }
                    entry->second = id;
                } catch (...) {
                    sessions_.erase(entry);
                    throw;
                }
            }

            // Ответ отправляется только после подготовки ресурсов. Повторный SIGUSR1
            // получает прежний ID, даже если предыдущий ответ потерялся.
            union sigval value{};
            value.sival_int = static_cast<int>(entry->second);
            std::error_code replyError;
            if (sigqueue(pid, SIGUSR2, value) == -1) {
                replyError = std::error_code(errno, std::generic_category());
            }
            return HandshakeResult{pid, entry->second, inserted, replyError};
        }
        if (deadline.remainingMilliseconds() == 0) {
            return std::nullopt;
        }
    }
}

void HandshakeServer::forget(pid_t clientPid) {
    sessions_.erase(clientPid);
}

} // namespace lab2::handshake
