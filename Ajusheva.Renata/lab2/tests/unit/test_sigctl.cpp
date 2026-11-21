#include "check.hpp"

#include "link/Signals.hpp"

#include <atomic>
#include <chrono>
#include <thread>

#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

using namespace chatlab;

namespace {

int childExit(pid_t child) {
    int status = 0;
    ::waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

sigset_t knockSet() {
    sigset_t set;
    ::sigemptyset(&set);
    ::sigaddset(&set, SIGUSR1);
    return set;
}

}

TEST(sigctl_knock_is_answered_with_slot) {
    sigctl::blockForThreads();
    const pid_t host = ::getpid();
    const pid_t child = ::fork();
    if (child == 0) {
        const sigctl::KnockResult result = sigctl::knock(host, std::chrono::milliseconds(3000));
        ::_exit(result.outcome == sigctl::KnockOutcome::Admitted ? result.slot : 100);
    }
    const sigset_t set = knockSet();
    siginfo_t info{};
    timespec wait{3, 0};
    const int signal = ::sigtimedwait(&set, &info, &wait);
    CHECK_EQ(signal, SIGUSR1);
    CHECK_EQ(info.si_pid, child);
    CHECK(sigctl::answer(child, 42));
    CHECK_EQ(childExit(child), 42);
}

TEST(sigctl_knock_without_answer_times_out) {
    sigctl::blockForThreads();
    const auto started = std::chrono::steady_clock::now();
    const sigctl::KnockResult result = sigctl::knock(::getpid(), std::chrono::milliseconds(400));
    CHECK(result.outcome == sigctl::KnockOutcome::NoAnswer);
    CHECK(std::chrono::steady_clock::now() - started >= std::chrono::milliseconds(380));
    const sigset_t set = knockSet();
    siginfo_t info{};
    timespec none{0, 0};
    while (::sigtimedwait(&set, &info, &none) > 0) {
    }
}

TEST(sigctl_knock_to_missing_process_fails_fast) {
    const pid_t child = ::fork();
    if (child == 0) {
        ::_exit(0);
    }
    childExit(child);
    const auto started = std::chrono::steady_clock::now();
    CHECK(sigctl::knock(child, std::chrono::milliseconds(3000)).outcome == sigctl::KnockOutcome::NoHost);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(500));
}

TEST(sigctl_watcher_reports_knocks_from_many_clients) {
    sigctl::blockForThreads();
    std::atomic<int> answered{0};
    sigctl::Watcher watcher(
        [&answered](pid_t guest) {
            sigctl::answer(guest, 7);
            ++answered;
        },
        [](int) {});
    watcher.start();
    pid_t children[4];
    for (pid_t& child : children) {
        child = ::fork();
        if (child == 0) {
            const sigctl::KnockResult result = sigctl::knock(::getppid(), std::chrono::milliseconds(4000));
            ::_exit(result.outcome == sigctl::KnockOutcome::Admitted ? result.slot : 100);
        }
    }
    for (const pid_t child : children) {
        CHECK_EQ(childExit(child), 7);
    }
    watcher.stop();
    CHECK(answered >= 4);
}

TEST(sigctl_quit_signal_is_seen) {
    sigctl::blockForThreads();
    CHECK(!sigctl::quitPending());
    ::kill(::getpid(), SIGTERM);
    CHECK(sigctl::quitPending());
    CHECK(!sigctl::quitPending());
}
