#include "Notifier.hpp"

#include <cstdlib>
#include <unistd.h>

namespace {

const char kCommand[] =
    "printf '%s\\n\\n' \"$1\"; "
    "printf 'Нажмите Enter, чтобы закрыть окно...'; "
    "read answer";

}

bool openTerminal(const std::string& text) {
    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid > 0) {
        return true;
    }

    const char* custom = std::getenv("REMINDER_TERMINAL");
    if (custom != nullptr && *custom != '\0') {
        execlp(custom, custom, "-e", "sh", "-c", kCommand, "reminder", text.c_str(),
               static_cast<char*>(nullptr));
        _exit(127);
    }

    execlp("x-terminal-emulator", "x-terminal-emulator", "-e", "sh", "-c", kCommand,
           "reminder", text.c_str(), static_cast<char*>(nullptr));
    execlp("xterm", "xterm", "-e", "sh", "-c", kCommand, "reminder", text.c_str(),
           static_cast<char*>(nullptr));
    execlp("konsole", "konsole", "-e", "sh", "-c", kCommand, "reminder", text.c_str(),
           static_cast<char*>(nullptr));
    execlp("gnome-terminal", "gnome-terminal", "--", "sh", "-c", kCommand, "reminder",
           text.c_str(), static_cast<char*>(nullptr));
    _exit(127);
}
