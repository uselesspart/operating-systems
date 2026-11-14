#include "daemon.hpp"

#include "cleaner.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

namespace {
const char* const kIdent = "dirclean_daemon";
}

Daemon::Daemon() : pid_(PidManager::defaultPath()) {}

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

bool Daemon::reloadConfig() {
    Config fresh;
    std::string error;
    if (!parseConfig(configPath_, fresh, error)) {
        syslog(LOG_ERR, "invalid configuration %s: %s", configPath_.c_str(), error.c_str());
        return false;
    }
    config_ = fresh;
    return true;
}

bool Daemon::daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork failed: %s", std::strerror(errno));
        return false;
    }
    if (pid > 0) _exit(0);  // родитель завершается

    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid failed: %s", std::strerror(errno));
        return false;
    }

    // второй fork: процесс не может стать лидером сессии и захватить терминал
    pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "second fork failed: %s", std::strerror(errno));
        return false;
    }
    if (pid > 0) _exit(0);

    umask(0);
    if (chdir("/") != 0) {
        syslog(LOG_ERR, "chdir failed: %s", std::strerror(errno));
        return false;
    }

    long maxFd = sysconf(_SC_OPEN_MAX);
    if (maxFd < 0 || maxFd > 65536) maxFd = 65536;
    for (int fd = 0; fd < maxFd; ++fd) close(fd);

    // дескрипторы 0, 1, 2 перенаправляем на /dev/null
    const int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        dup2(fd, 0);
        dup2(fd, 1);
        dup2(fd, 2);
        if (fd > 2) close(fd);
    }
    return true;
}

void Daemon::cleanup() {
    const CleanResult r = removeSubdirectories(config_.dir1);
    if (!r.ok())
        syslog(LOG_ERR, "cleanup of %s: %s (removed %zu, failed %zu)", config_.dir1.c_str(),
               r.error.c_str(), r.removed, r.failed);
    else
        syslog(LOG_INFO, "cleanup of %s: removed %zu subdirectories", config_.dir1.c_str(),
               r.removed);
}

int Daemon::loop() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);

    using clock = std::chrono::steady_clock;
    auto next = clock::now();  // первая проверка — сразу после запуска

    for (;;) {
        auto now = clock::now();
        if (now >= next) {
            cleanup();
            now = clock::now();
            next = now + std::chrono::seconds(config_.interval);
        }

        const auto left = std::chrono::duration_cast<std::chrono::nanoseconds>(next - now);
        timespec ts{};
        ts.tv_sec = static_cast<time_t>(left.count() / 1000000000LL);
        ts.tv_nsec = static_cast<long>(left.count() % 1000000000LL);

        const int sig = sigtimedwait(&set, nullptr, &ts);
        if (sig == SIGTERM) {
            syslog(LOG_INFO, "received SIGTERM, terminating");
            pid_.removeIfOwnedBy(getpid());
            return 0;
        }
        if (sig == SIGHUP) {
            syslog(LOG_INFO, "received SIGHUP, reloading %s", configPath_.c_str());
            if (reloadConfig()) {
                syslog(LOG_INFO, "configuration reloaded (interval %u, dir1 %s)", config_.interval,
                       config_.dir1.c_str());
                next = clock::now();  // сразу применяем новый конфиг
            }
        } else if (sig < 0 && errno != EAGAIN && errno != EINTR) {
            syslog(LOG_ERR, "sigtimedwait failed: %s", std::strerror(errno));
            return 1;
        }
    }
}

int Daemon::run(const std::string& configPath) {
    openlog(kIdent, LOG_PID, LOG_DAEMON);

    // Абсолютный путь запоминаем до chdir("/"), иначе SIGHUP не найдёт конфиг.
    configPath_ = absolutePath(configPath);
    if (!reloadConfig()) {
        std::fprintf(stderr, "dirclean_daemon: invalid configuration %s\n", configPath_.c_str());
        closelog();
        return 1;
    }

    // Сигналы блокируем заранее (маска наследуется через fork), чтобы между
    // записью pid-файла и входом в цикл ни один SIGHUP/SIGTERM не пропал.
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    sigprocmask(SIG_BLOCK, &set, nullptr);

    if (!pid_.stopOldInstance()) {
        syslog(LOG_ERR, "cannot stop the previous instance (pid file %s)", pid_.path().c_str());
        closelog();
        return 1;
    }

    if (!daemonize()) {
        closelog();
        return 1;
    }

    // после закрытия дескрипторов соединение с журналом нужно открыть заново
    closelog();
    openlog(kIdent, LOG_PID | LOG_NDELAY, LOG_DAEMON);

    if (!pid_.writePid(getpid())) {
        syslog(LOG_ERR, "cannot write pid file %s", pid_.path().c_str());
        closelog();
        return 1;
    }
    syslog(LOG_INFO, "daemon started (pid %d), config %s", static_cast<int>(getpid()),
           configPath_.c_str());

    const int code = loop();
    syslog(LOG_INFO, "daemon stopped");
    closelog();
    return code;
}
