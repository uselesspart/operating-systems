#include "TerminalNotifier.hpp"

#include <cerrno>
#include <cstring>
#include <syslog.h>
#include <sys/wait.h>
#include <unistd.h>

namespace reminder {
void TerminalNotifier::reapChildren() {
    int status;
    pid_t child;
    while ((child = waitpid(-1, &status, WNOHANG)) > 0) {
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            syslog(LOG_ERR, "Terminal child %ld failed", static_cast<long>(child));
        }
    }
}

void TerminalNotifier::show(const std::string& message, const sigset_t& originalMask) const {
    const pid_t child = fork();
    if (child < 0) {
        syslog(LOG_ERR, "Cannot fork terminal: %s", std::strerror(errno));
        return;
    }
    if (child != 0) return;
    sigprocmask(SIG_SETMASK, &originalMask, nullptr);
    constexpr const char* display =
        "printf '\\nНапоминание:\\n%s\\n\\nНажмите Enter для закрытия...' \"$1\"; read -r answer";
    // xterm and x-terminal-emulator accept -e followed by an argument vector.
    execlp(terminal_.c_str(), terminal_.c_str(), "-e", "sh", "-c",
           display, "reminder-window", message.c_str(), static_cast<char*>(nullptr));
    syslog(LOG_ERR, "Cannot execute terminal %s: %s", terminal_.c_str(), std::strerror(errno));
    _exit(127);
}
}
