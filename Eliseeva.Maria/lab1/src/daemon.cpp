#include "daemon.hpp"

#include "cleaner.hpp"
#include "pid_manager.hpp"

#include <cerrno>
#include <climits>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

namespace dirclean {

namespace {

const char *const kIdent = "dirclean_daemon";
const char *const kDefaultPidFile = "/tmp/dirclean_daemon.pid";

// SIGHUP и SIGTERM блокируются и принимаются через sigtimedwait в главном цикле:
// так сигнал не теряется между проверкой флага и засыпанием.
void blockSignals(sigset_t *set) {
    sigemptyset(set);
    sigaddset(set, SIGHUP);
    sigaddset(set, SIGTERM);
    sigprocmask(SIG_BLOCK, set, nullptr);
}

} 

Daemon &Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

std::string Daemon::defaultPidFilePath() {
    const char *env = std::getenv("DIRCLEAN_PID_FILE");
    return (env && *env) ? env : kDefaultPidFile;
}

bool Daemon::setConfigPath(const std::string &path) {
    char resolved[PATH_MAX];
    if (!realpath(path.c_str(), resolved)) return false;
    configPath_ = resolved;
    return true;
}

bool Daemon::reloadConfig() {
    if (configPath_.empty()) return false;

    Config cfg;
    std::vector<std::string> warnings;
    if (!loadConfigFile(configPath_, cfg, &warnings)) {
        syslog(LOG_ERR, "Cannot open config file %s", configPath_.c_str());
        return false;
    }
    for (const std::string &w : warnings) syslog(LOG_WARNING, "%s", w.c_str());

    config_ = std::move(cfg);
    syslog(LOG_INFO, "Config loaded: %zu rule(s), interval %us", config_.rules.size(),
           config_.interval);
    return true;
}

std::size_t Daemon::processRules() {
    std::size_t cleaned = 0;
    for (const Rule &rule : config_.rules) {
        CleanReport rep = applyRule(rule);
        switch (rep.status) {
            case CleanStatus::Cleaned:
                ++cleaned;
                syslog(LOG_INFO, "Cleaned %s (%zu entries removed)", rep.folder.c_str(),
                       rep.removed);
                if (rep.failed > 0)
                    syslog(LOG_WARNING, "%zu entries not removed: %s", rep.failed,
                           rep.message.c_str());
                break;
            case CleanStatus::Ignored:
                break;
            case CleanStatus::NotDirectory:
                syslog(LOG_WARNING, "'%s' is not a directory, skipped", rule.folder.c_str());
                break;
            case CleanStatus::Refused:
                syslog(LOG_ERR, "Refusing to clean root directory");
                break;
            case CleanStatus::Error:
                syslog(LOG_ERR, "%s: %s", rep.folder.c_str(), rep.message.c_str());
                break;
        }
    }
    return cleaned;
}

int Daemon::run(const std::string &configArg) {
    openlog(kIdent, LOG_PID, LOG_DAEMON);

    sigset_t sigs;
    blockSignals(&sigs);

    // Абсолютный путь запоминаем до смены рабочей директории на "/".
    if (!setConfigPath(configArg)) {
        syslog(LOG_ERR, "Config file '%s' not found: %s", configArg.c_str(), strerror(errno));
        fprintf(stderr, "Config file '%s' not found: %s\n", configArg.c_str(), strerror(errno));
        closelog();
        return 1;
    }
    if (!reloadConfig()) {
        fprintf(stderr, "Cannot read config '%s'\n", configPath_.c_str());
        closelog();
        return 1;
    }

    PidManager pidManager(defaultPidFilePath());
    if (!pidManager.acquireStartupLock()) {
        syslog(LOG_ERR, "Cannot lock pid file %s: %s", pidManager.path().c_str(),
               strerror(errno));
        closelog();
        return 1;
    }

    TerminateResult prev = pidManager.terminatePrevious(std::chrono::seconds(5));
    if (prev.status == TerminateStatus::Terminated) {
        syslog(LOG_INFO, "Found running instance pid %ld, SIGTERM sent, it has exited", prev.pid);
    } else if (prev.status == TerminateStatus::StillRunning) {
        syslog(LOG_ERR, "Found running instance pid %ld, it did not exit after SIGTERM", prev.pid);
        fprintf(stderr, "Previous instance (pid %ld) did not exit\n", prev.pid);
        closelog();
        return 1;
    } else if (prev.status == TerminateStatus::SignalFailed) {
        syslog(LOG_ERR, "Cannot send SIGTERM to previous instance pid %ld: %s", prev.pid,
               strerror(prev.errorCode));
        fprintf(stderr, "Cannot stop previous instance (pid %ld)\n", prev.pid);
        closelog();
        return 1;
    }

    daemonize();

    if (!pidManager.write(getpid())) {
        syslog(LOG_ERR, "Cannot write pid file %s", pidManager.path().c_str());
        closelog();
        return 1;
    }
    pidManager.releaseStartupLock();

    syslog(LOG_INFO, "Daemon started (config: %s, interval: %us)", configPath_.c_str(),
           config_.interval);

    mainLoop(pidManager);
    return 0;
}

void Daemon::daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork failed: %s", strerror(errno));
        exit(1);
    }
    if (pid > 0) _exit(0);  // родитель завершается

    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid failed: %s", strerror(errno));
        exit(1);
    }
    umask(0);
    if (chdir("/") < 0) {
        syslog(LOG_ERR, "chdir failed: %s", strerror(errno));
        exit(1);
    }

    long maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 4096) maxfd = 4096;
    for (int i = 0; i < maxfd; ++i) close(i);

    // Дескрипторы 0, 1, 2 снова открываем на /dev/null.
    int fd = open("/dev/null", O_RDWR);
    if (fd >= 0) {
        if (dup(fd) < 0 || dup(fd) < 0) { /* без stdout/stderr можно жить */ }
    }

    // close() закрыл и сокет syslog - открываем заново.
    openlog(kIdent, LOG_PID, LOG_DAEMON);
}

void Daemon::mainLoop(const PidManager &pidManager) {
    sigset_t sigs;
    blockSignals(&sigs);

    unsigned elapsed = config_.interval;  // первое действие - сразу после старта
    bool running = true;
    while (running) {
        if (elapsed >= config_.interval) {
            processRules();
            elapsed = 0;
        }

        struct timespec timeout = {1, 0};
        int sig = sigtimedwait(&sigs, nullptr, &timeout);
        if (sig == SIGTERM) {
            running = false;
        } else if (sig == SIGHUP) {
            syslog(LOG_INFO, "SIGHUP received, reloading config");
            if (!reloadConfig()) syslog(LOG_ERR, "Reload failed, keeping old config");
            elapsed = config_.interval;  // применить новую конфигурацию сразу
        } else {
            ++elapsed;  // прошла секунда (или прервались по EINTR)
        }
    }

    syslog(LOG_INFO, "SIGTERM received, daemon exiting");
    pidManager.removeIfOwner(getpid());
    closelog();
}

}  