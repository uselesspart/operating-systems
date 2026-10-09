#ifndef CHAT_SIGNALS_H
#define CHAT_SIGNALS_H

#include "ipc/Timeouts.h"

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include <csignal>
#include <sys/types.h>

namespace chat {

constexpr int kHandshakeSignal = SIGUSR1;

// Blocks SIGUSR1, SIGINT and SIGTERM so they are only taken by sigtimedwait.
// Must be called in main before any thread is created
void blockProcessSignals();

// Two-way handshake, client side: SIGUSR1 to the host, then a SIGUSR1 back from the host
// (sigqueue) whose value is the client id. Repeats the request because standard signals coalesce
enum class HandshakeStatus { Accepted, NoHost, Timeout };

struct HandshakeResult {
    HandshakeStatus status;
    int clientId = 0;
};

HandshakeResult requestHandshake(pid_t host, std::chrono::milliseconds timeout = kIpcTimeout);

// Two-way handshake, host side: tells the client its id once its channel exists
bool acceptHandshake(pid_t client, int clientId);

// Consumes a pending SIGINT or SIGTERM, if any
bool takeTerminationRequest();

// Host side: dedicated thread that receives handshake requests and termination signals
class SignalListener {
public:
    using HandshakeHandler = std::function<void(pid_t client)>;
    using TerminateHandler = std::function<void(int signal)>;

    SignalListener(HandshakeHandler onHandshake, TerminateHandler onTerminate);
    ~SignalListener();

    SignalListener(const SignalListener&) = delete;
    SignalListener& operator=(const SignalListener&) = delete;

    void start();
    void stop();

private:
    void run();

    HandshakeHandler onHandshake_;
    TerminateHandler onTerminate_;
    std::atomic<bool> running_{false};
    std::thread thread_;
};

}

#endif // CHAT_SIGNALS_H
