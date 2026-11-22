#ifndef CHAT_TESTS_FORKED_PROCESS_H
#define CHAT_TESTS_FORKED_PROCESS_H

#include "ipc/UniqueFd.h"

#include <chrono>
#include <exception>
#include <functional>
#include <optional>
#include <thread>

#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace chat::testing {

using namespace std::chrono_literals;

// Second process for IPC tests. It is forked before the parent sets up its side, so it shares
// no descriptors with it, like an independent program. The body runs after start()
class ForkedProcess {
public:
    using Body = std::function<bool()>;

    explicit ForkedProcess(const Body& body)
    {
        int gate[2];
        if (::pipe2(gate, O_CLOEXEC) != 0) {
            return;
        }
        UniqueFd gateIn(gate[0]);
        gate_.reset(gate[1]);

        pid_ = ::fork();
        if (pid_ == 0) {
            gate_.reset();
            char go = 0;
            if (::read(gateIn.get(), &go, 1) != 1) {
                ::_exit(kNotStarted);
            }
            gateIn.reset();
            ::_exit(runBody(body));
        }
    }

    ~ForkedProcess()
    {
        gate_.reset();
        if (pid_ > 0 && !waitExit(0ms)) {
            ::kill(pid_, SIGKILL);
            ::waitpid(pid_, nullptr, 0);
        }
    }

    ForkedProcess(const ForkedProcess&) = delete;
    ForkedProcess& operator=(const ForkedProcess&) = delete;

    [[nodiscard]] pid_t pid() const noexcept { return pid_; }

    void start()
    {
        const char go = 1;
        if (::write(gate_.get(), &go, 1) != 1 && pid_ > 0) {
            ::kill(pid_, SIGKILL);
        }
        gate_.reset();
    }

    // Exit code, 128 + signal number if killed, nothing if still running after the timeout
    std::optional<int> waitExit(std::chrono::milliseconds timeout)
    {
        if (exitCode_ || pid_ <= 0) {
            return exitCode_;
        }
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (true) {
            int status = 0;
            if (::waitpid(pid_, &status, WNOHANG) == pid_) {
                exitCode_ = WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
                return exitCode_;
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                return std::nullopt;
            }
            std::this_thread::sleep_for(5ms);
        }
    }

    bool succeeded(std::chrono::milliseconds timeout = 10s) { return waitExit(timeout) == 0; }

private:
    static constexpr int kNotStarted = 2;
    static constexpr int kThrew = 3;

    static int runBody(const Body& body)
    {
        try {
            return body() ? 0 : 1;
        } catch (const std::exception&) {
            return kThrew;
        }
    }

    pid_t pid_ = -1;
    UniqueFd gate_;
    std::optional<int> exitCode_;
};

}

#endif // CHAT_TESTS_FORKED_PROCESS_H
