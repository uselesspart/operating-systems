#include "Daemon.hpp"

#include "Config.hpp"
#include "Notifier.hpp"
#include "PidFile.hpp"

#include <csignal>
#include <cstddef>
#include <ctime>
#include <fcntl.h>
#include <iostream>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>
#include <vector>

namespace {

constexpr unsigned int kCheckInterval = 10;

volatile sig_atomic_t g_stop = 0;
volatile sig_atomic_t g_reload = 0;

void handleStop(int) {
    g_stop = 1;
}

void handleReload(int) {
    g_reload = 1;
}

void installHandlers() {
    struct sigaction action {};
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    action.sa_handler = handleStop;
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGINT, &action, nullptr);

    action.sa_handler = handleReload;
    sigaction(SIGHUP, &action, nullptr);
}

void reapChildren() {
    while (waitpid(-1, nullptr, WNOHANG) > 0) {
    }
}

}

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

bool Daemon::daemonize() {
    signal(SIGHUP, SIG_IGN);

    pid_t pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid > 0) {
        _exit(0);
    }

    if (setsid() < 0) {
        return false;
    }

    pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid > 0) {
        _exit(0);
    }

    umask(027);
    if (chdir("/") != 0) {
        return false;
    }

    int nullFd = open("/dev/null", O_RDWR);
    if (nullFd < 0) {
        return false;
    }
    dup2(nullFd, STDIN_FILENO);
    dup2(nullFd, STDOUT_FILENO);
    dup2(nullFd, STDERR_FILENO);
    if (nullFd > STDERR_FILENO) {
        close(nullFd);
    }

    long maxFd = sysconf(_SC_OPEN_MAX);
    if (maxFd < 0 || maxFd > 1024) {
        maxFd = 1024;
    }
    for (int fd = 3; fd < maxFd; ++fd) {
        close(fd);
    }
    return true;
}

int Daemon::run(const std::string& configPath) {
    std::string error;
    if (!stopRunningInstance(error)) {
        std::cerr << "reminder_daemon: " << error << std::endl;
        return 1;
    }

    if (!daemonize()) {
        return 1;
    }

    openlog("reminder_daemon", LOG_PID, LOG_DAEMON);
    installHandlers();

    if (!writePidFile(error)) {
        syslog(LOG_ERR, "%s", error.c_str());
        closelog();
        return 1;
    }

    syslog(LOG_INFO, "демон запущен, pid %d, конфигурация %s", static_cast<int>(getpid()),
           configPath.c_str());

    std::vector<Event> events;
    const std::string lockPath = configPath + ".lock";
    bool lockWarned = false;

    while (!g_stop) {
        reapChildren();

        if (g_reload) {
            g_reload = 0;
            syslog(LOG_INFO, "получен SIGHUP, перечитываю конфигурацию");
        }

        int lockFd = open(lockPath.c_str(), O_CREAT | O_RDWR, 0600);
        if (lockFd >= 0 && flock(lockFd, LOCK_EX) != 0) {
            close(lockFd);
            lockFd = -1;
        }
        if (lockFd < 0 && !lockWarned) {
            syslog(LOG_WARNING, "не удалось заблокировать %s", lockPath.c_str());
            lockWarned = true;
        }

        std::vector<Event> loaded;
        if (loadEvents(configPath, loaded, error)) {
            events.swap(loaded);
            long long now = static_cast<long long>(std::time(nullptr));
            bool changed = false;

            for (std::size_t index = 0; index < events.size();) {
                if (events[index].timestamp > now) {
                    ++index;
                    continue;
                }

                std::string terminalError;
                if (!openTerminal(events[index].text, terminalError)) {
                    syslog(LOG_ERR, "не удалось открыть терминал для напоминания: %s (%s)",
                           events[index].text.c_str(), terminalError.c_str());
                    ++index;
                    continue;
                }

                syslog(LOG_INFO, "напоминание: %s", events[index].text.c_str());

                if (events[index].repeat == Repeat::None) {
                    events.erase(events.begin() + static_cast<std::ptrdiff_t>(index));
                } else {
                    moveToFuture(events[index], now);
                    ++index;
                }
                changed = true;
            }

            if (changed && !saveEvents(configPath, events, error)) {
                syslog(LOG_ERR, "%s", error.c_str());
            }
        } else {
            syslog(LOG_ERR, "%s", error.c_str());
        }

        if (lockFd >= 0) {
            flock(lockFd, LOCK_UN);
            close(lockFd);
        }

        sleep(kCheckInterval);
    }

    syslog(LOG_INFO, "завершение работы по сигналу");
    removePidFile();
    closelog();
    return 0;
}
