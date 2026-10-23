#include "ipc/Signals.h"

#include <algorithm>
#include <cerrno>
#include <utility>

#include <pthread.h>
#include <signal.h>

namespace chat {

namespace {

using Clock = std::chrono::steady_clock;

constexpr auto kResendInterval = std::chrono::seconds(1);
constexpr auto kListenerPollInterval = std::chrono::milliseconds(200);

sigset_t makeSet(std::initializer_list<int> signals)
{
    sigset_t set;
    sigemptyset(&set);
    for (const int signal : signals) {
        sigaddset(&set, signal);
    }
    return set;
}

timespec toTimespec(Clock::duration duration)
{
    const auto nanos =
        std::max<long long>(0, std::chrono::duration_cast<std::chrono::nanoseconds>(duration).count());
    timespec result{};
    result.tv_sec = static_cast<time_t>(nanos / 1'000'000'000LL);
    result.tv_nsec = static_cast<long>(nanos % 1'000'000'000LL);
    return result;
}

int waitSignal(const sigset_t& set, siginfo_t& info, Clock::duration timeout)
{
    const timespec wait = toTimespec(timeout);
    return ::sigtimedwait(&set, &info, &wait);
}

}

void blockProcessSignals()
{
    const sigset_t set = makeSet({kHandshakeSignal, SIGINT, SIGTERM});
    pthread_sigmask(SIG_BLOCK, &set, nullptr);
}

HandshakeResult requestHandshake(pid_t host, std::chrono::milliseconds timeout)
{
    const sigset_t set = makeSet({kHandshakeSignal});
    const auto deadline = Clock::now() + timeout;
    auto nextRequest = Clock::now();

    for (auto now = Clock::now(); now < deadline; now = Clock::now()) {
        if (now >= nextRequest) {
            if (::kill(host, kHandshakeSignal) != 0) {
                return {HandshakeStatus::NoHost};
            }
            nextRequest = now + kResendInterval;
        }
        siginfo_t info{};
        const int signal = waitSignal(set, info, std::min(deadline, nextRequest) - now);
        if (signal == kHandshakeSignal && info.si_pid == host && info.si_code == SI_QUEUE) {
            return {HandshakeStatus::Accepted, info.si_value.sival_int};
        }
    }
    return {HandshakeStatus::Timeout};
}

bool acceptHandshake(pid_t client, int clientId)
{
    sigval value{};
    value.sival_int = clientId;
    return ::sigqueue(client, kHandshakeSignal, value) == 0;
}

bool takeTerminationRequest()
{
    const sigset_t set = makeSet({SIGINT, SIGTERM});
    siginfo_t info{};
    return waitSignal(set, info, Clock::duration::zero()) > 0;
}

SignalListener::SignalListener(HandshakeHandler onHandshake, TerminateHandler onTerminate)
    : onHandshake_(std::move(onHandshake)), onTerminate_(std::move(onTerminate))
{
}

SignalListener::~SignalListener()
{
    stop();
}

void SignalListener::start()
{
    running_ = true;
    thread_ = std::thread(&SignalListener::run, this);
}

void SignalListener::stop()
{
    running_ = false;
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SignalListener::run()
{
    const sigset_t set = makeSet({kHandshakeSignal, SIGINT, SIGTERM});
    while (running_) {
        siginfo_t info{};
        const int signal = waitSignal(set, info, kListenerPollInterval);
        if (signal == kHandshakeSignal && info.si_code == SI_USER) {
            onHandshake_(info.si_pid);
        } else if (signal == SIGINT || signal == SIGTERM) {
            onTerminate_(signal);
        }
    }
}

}
