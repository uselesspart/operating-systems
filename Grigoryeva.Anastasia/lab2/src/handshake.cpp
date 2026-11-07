#include "handshake.h"

#include <pthread.h>
#include <signal.h>
#include <time.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstring>

namespace {

sigset_t HandshakeSet() {
    sigset_t s;
    sigemptyset(&s);
    sigaddset(&s, SIGUSR1);
    return s;
}

struct timespec ToTimespec(int ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = static_cast<long>(ms % 1000) * 1000000L;
    return ts;
}

}  // namespace

void BlockHandshakeSignal() {
    sigset_t s = HandshakeSet();
    pthread_sigmask(SIG_BLOCK, &s, nullptr);
}

bool HostWaitRequest(int timeoutMs, pid_t& clientPid) {
    sigset_t s = HandshakeSet();
    struct timespec ts = ToTimespec(timeoutMs);
    siginfo_t info;
    if (sigtimedwait(&s, &info, &ts) < 0) return false;  // тайм-аут или прерывание
    clientPid = info.si_pid;
    return clientPid > 0;
}

bool HostReply(pid_t clientPid, int id) {
    union sigval v;
    v.sival_int = id;
    return sigqueue(clientPid, SIGUSR1, v) == 0;
}

int ClientHandshake(pid_t hostPid, int timeoutMs, std::string& err) {
    using clock = std::chrono::steady_clock;
    const sigset_t s = HandshakeSet();
    const int kResendMs = 1500;
    const auto start = clock::now();

    auto elapsedMs = [&]() {
        return static_cast<int>(std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - start).count());
    };

    while (true) {
        if (kill(hostPid, SIGUSR1) != 0) {
            err = "не удалось отправить сигнал хосту (pid " + std::to_string(static_cast<long>(hostPid)) +
                  "): " + std::strerror(errno);
            return -1;
        }
        const int sliceEnd = std::min(timeoutMs, elapsedMs() + kResendMs);
        while (elapsedMs() < sliceEnd) {
            struct timespec ts = ToTimespec(sliceEnd - elapsedMs());
            siginfo_t info;
            if (sigtimedwait(&s, &info, &ts) < 0) continue;  // EAGAIN (тайм-аут среза) / EINTR
            if (info.si_pid != hostPid || info.si_code != SI_QUEUE) continue;  // чужой или наш же сигнал
            int id = info.si_value.sival_int;
            if (id <= 0) {
                err = "хост отклонил подключение";
                return -1;
            }
            return id;
        }
        if (elapsedMs() >= timeoutMs) {
            err = "тайм-аут знакомства: хост не ответил за " + std::to_string(timeoutMs) + " мс";
            return -1;
        }
    }
}
