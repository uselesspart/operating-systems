#include "ChildProcess.hpp"

#include <cerrno>
#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

namespace chat {
ChildProcess::~ChildProcess() {
    if (pid_ <= 0) return;
    // Checking waitpid first also avoids signalling a PID already reaped by the GUI.
    for (int i = 0; i < 125; ++i) {
        const auto result = waitpid(pid_, nullptr, WNOHANG);
        if (result == pid_ || (result < 0 && errno == ECHILD)) return;
        if (i == 0) kill(pid_, SIGTERM);
        usleep(20000);
    }
    kill(pid_, SIGKILL);
    for (int i = 0; i < 125; ++i) {
        const auto result = waitpid(pid_, nullptr, WNOHANG);
        if (result == pid_ || (result < 0 && errno == ECHILD)) return;
        usleep(20000);
    }
}
}
