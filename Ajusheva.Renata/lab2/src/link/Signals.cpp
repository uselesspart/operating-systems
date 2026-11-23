#include "link/Signals.hpp"

#include <algorithm>
#include <initializer_list>
#include <utility>

#include <pthread.h>
#include <signal.h>

namespace chatlab::sigctl {

namespace {

using Steady = std::chrono::steady_clock;

constexpr auto kRepeatKnock = std::chrono::milliseconds(1000);
constexpr auto kWatchSlice = std::chrono::milliseconds(200);

sigset_t setOf(std::initializer_list<int> list) {
    sigset_t set;
    ::sigemptyset(&set);
    for (const int signal : list) {
        ::sigaddset(&set, signal);
    }
    return set;
}

const sigset_t& quitSet() {
    static const sigset_t set = setOf({SIGINT, SIGTERM, SIGHUP});
    return set;
}

int waitOne(const sigset_t& set, siginfo_t& info, Steady::duration span) {
    const auto nanos = std::max<long long>(
        0, std::chrono::duration_cast<std::chrono::nanoseconds>(span).count());
    timespec wait{};
    wait.tv_sec = static_cast<time_t>(nanos / 1000000000LL);
    wait.tv_nsec = static_cast<long>(nanos % 1000000000LL);
    return ::sigtimedwait(&set, &info, &wait);
}

bool isQuit(int signal) {
    return signal == SIGINT || signal == SIGTERM || signal == SIGHUP;
}

}

void blockForThreads() {
    const sigset_t set = setOf({kKnock, SIGINT, SIGTERM, SIGHUP});
    ::pthread_sigmask(SIG_BLOCK, &set, nullptr);
    ::signal(SIGPIPE, SIG_IGN);
}

KnockResult knock(pid_t host, std::chrono::milliseconds limit) {
    const sigset_t set = setOf({kKnock, SIGINT, SIGTERM, SIGHUP});
    const auto deadline = Steady::now() + limit;
    auto nextKnock = Steady::now();
    for (auto now = Steady::now(); now < deadline; now = Steady::now()) {
        if (now >= nextKnock) {
            if (::kill(host, kKnock) != 0) {
                return {KnockOutcome::NoHost};
            }
            nextKnock = now + kRepeatKnock;
        }
        siginfo_t info{};
        const int signal = waitOne(set, info, std::min(deadline, nextKnock) - now);
        if (isQuit(signal)) {
            return {KnockOutcome::Interrupted};
        }
        if (signal == kKnock && info.si_code == SI_QUEUE && info.si_pid == host) {
            return {KnockOutcome::Admitted, info.si_value.sival_int};
        }
    }
    return {KnockOutcome::NoAnswer};
}

bool answer(pid_t guest, int slot) {
    sigval value{};
    value.sival_int = slot;
    return ::sigqueue(guest, kKnock, value) == 0;
}

bool quitPending() {
    siginfo_t info{};
    return waitOne(quitSet(), info, Steady::duration::zero()) > 0;
}

Watcher::Watcher(KnockHandler onKnock, QuitHandler onQuit)
    : onKnock_(std::move(onKnock)), onQuit_(std::move(onQuit)) {}

Watcher::~Watcher() {
    stop();
}

void Watcher::start() {
    active_ = true;
    worker_ = std::thread(&Watcher::loop, this);
}

void Watcher::stop() {
    active_ = false;
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Watcher::loop() {
    const sigset_t set = setOf({kKnock, SIGINT, SIGTERM, SIGHUP});
    while (active_) {
        siginfo_t info{};
        const int signal = waitOne(set, info, kWatchSlice);
        if (signal == kKnock && info.si_code == SI_USER) {
            onKnock_(info.si_pid);
        } else if (isQuit(signal)) {
            onQuit_(signal);
        }
    }
}

}
