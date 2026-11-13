#include "Notifier.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {

const char kCommand[] =
    "printf '%s\\n\\n' \"$1\"; "
    "printf 'Нажмите Enter, чтобы закрыть окно...'; "
    "read answer";

bool setCloseOnExec(int fd) {
    int flags = fcntl(fd, F_GETFD);
    if (flags < 0) {
        return false;
    }
    return fcntl(fd, F_SETFD, flags | FD_CLOEXEC) == 0;
}

void reportExecFailure(int fd, int code) {
    ssize_t ignored = write(fd, &code, sizeof code);
    (void)ignored;
    _exit(127);
}

}

bool openTerminal(const std::string& text, std::string& error) {
    error.clear();

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        error = std::string("pipe: ") + std::strerror(errno);
        return false;
    }
    if (!setCloseOnExec(pipefd[1])) {
        error = std::string("fcntl: ") + std::strerror(errno);
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    pid_t pid = fork();
    if (pid < 0) {
        error = std::string("fork: ") + std::strerror(errno);
        close(pipefd[0]);
        close(pipefd[1]);
        return false;
    }

    if (pid == 0) {
        close(pipefd[0]);

        const char* custom = std::getenv("REMINDER_TERMINAL");
        if (custom != nullptr && *custom != '\0') {
            execlp(custom, custom, "-e", "sh", "-c", kCommand, "reminder", text.c_str(),
                   static_cast<char*>(nullptr));
            reportExecFailure(pipefd[1], errno);
        }

        execlp("x-terminal-emulator", "x-terminal-emulator", "-e", "sh", "-c", kCommand,
               "reminder", text.c_str(), static_cast<char*>(nullptr));
        execlp("xterm", "xterm", "-e", "sh", "-c", kCommand, "reminder", text.c_str(),
               static_cast<char*>(nullptr));
        execlp("konsole", "konsole", "-e", "sh", "-c", kCommand, "reminder", text.c_str(),
               static_cast<char*>(nullptr));
        execlp("gnome-terminal", "gnome-terminal", "--", "sh", "-c", kCommand, "reminder",
               text.c_str(), static_cast<char*>(nullptr));
        reportExecFailure(pipefd[1], errno);
    }

    close(pipefd[1]);

    int childError = 0;
    ssize_t received;
    do {
        received = read(pipefd[0], &childError, sizeof childError);
    } while (received < 0 && errno == EINTR);
    close(pipefd[0]);

    if (received > 0) {
        waitpid(pid, nullptr, 0);
        error = std::string("не удалось запустить терминал: ") + std::strerror(childError);
        return false;
    }
    if (received < 0) {
        int saved = errno;
        waitpid(pid, nullptr, 0);
        error = std::string("ошибка чтения канала терминала: ") + std::strerror(saved);
        return false;
    }
    return true;
}
