#pragma once

#include "link/Timeouts.hpp"

#include <atomic>
#include <chrono>
#include <functional>
#include <thread>

#include <csignal>
#include <sys/types.h>

namespace chatlab::sigctl {

constexpr int kKnock = SIGUSR1;

void blockForThreads();

enum class KnockOutcome { Admitted, NoHost, NoAnswer, Interrupted };

struct KnockResult {
    KnockOutcome outcome;
    int slot = 0;
};

KnockResult knock(pid_t host, std::chrono::milliseconds limit = kWaitLimit);
bool answer(pid_t guest, int slot);
bool quitPending();

class Watcher {
  public:
    using KnockHandler = std::function<void(pid_t guest)>;
    using QuitHandler = std::function<void(int signal)>;

    Watcher(KnockHandler onKnock, QuitHandler onQuit);
    ~Watcher();

    Watcher(const Watcher&) = delete;
    Watcher& operator=(const Watcher&) = delete;

    void start();
    void stop();

  private:
    void loop();

    KnockHandler onKnock_;
    QuitHandler onQuit_;
    std::atomic<bool> active_{false};
    std::thread worker_;
};

}
