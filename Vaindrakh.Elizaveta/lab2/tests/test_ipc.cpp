#include "connection.hpp"
#include <cerrno>
#include <chrono>
#include <csignal>
#include <fcntl.h>
#include <iostream>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
void noop(int) {}
class Child {
public:
    explicit Child(pid_t pid) : pid_(pid) { check(pid != -1, "fork failed"); }
    ~Child() { if (pid_ > 0) { kill(pid_, SIGKILL); while (waitpid(pid_, nullptr, 0) == -1 && errno == EINTR) {} } }
    void wait() {
        int status = 0;
        pid_t result;
        do { result = waitpid(pid_, &status, 0); } while (result == -1 && errno == EINTR);
        pid_ = -1;
        check(result > 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0, "Child failed");
    }
private:
    pid_t pid_;
};
}
int main() {
    try {
        signal(SIGPIPE, SIG_IGN);
        {
            auto conn = lab::createConnection();
            const pid_t pid = fork();
            if (pid == 0) {
                int code = 0;
                try {
                    conn->afterFork(lab::Role::Client);
                    for (int i = 1; i <= 1000; ++i) {
                        const auto message = conn->Read();
                        lab::expect(message, lab::Kind::Roll, i);
                        check(message.value == i * 7, "Payload corrupted");
                        conn->Write({lab::Kind::Number, i, message.value + 1, i % 2, 0});
                    }
                } catch (const std::exception& error) { std::cerr << error.what() << '\n'; code = 1; }
                conn.reset();
                _exit(code);
            }
            Child child(pid);
            conn->afterFork(lab::Role::Host);
            for (int i = 1; i <= 1000; ++i) {
                conn->Write({lab::Kind::Roll, i, i * 7, 1, 0});
                const auto answer = conn->Read();
                lab::expect(answer, lab::Kind::Number, i);
                check(answer.value == i * 7 + 1 && answer.alive == i % 2, "Response corrupted");
            }
            child.wait();
        }
        {
            auto conn = lab::createConnection();
            conn->afterFork(lab::Role::Host);
            struct sigaction action{};
            action.sa_handler = noop;
            sigemptyset(&action.sa_mask);
            sigaction(SIGALRM, &action, nullptr);
            alarm(1); // EINTR must not extend sem_timedwait's deadline.
            bool timeout = false;
            const auto start = std::chrono::steady_clock::now();
            try { conn->Read(); } catch (const std::runtime_error& error) {
                timeout = std::string(error.what()).find("timeout") != std::string::npos;
            }
            const auto elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            check(timeout && elapsed >= 4.8 && elapsed < 6.5, "Five-second timeout failed");
        }
        const std::string name = "/wolf_lab2_" + std::to_string(getpid());
        const int descriptor = shm_open(name.c_str(), O_RDWR, 0600);
        if (descriptor != -1) { close(descriptor); throw std::runtime_error("Shared-memory object leaked"); }
        std::cout << TYPE_CODE " IPC: 1000 round trips, EINTR, 5-second timeout, cleanup: OK\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
