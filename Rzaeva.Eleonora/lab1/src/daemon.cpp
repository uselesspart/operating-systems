#include "daemon.hpp"

#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <fcntl.h>
#include <syslog.h>
#include <unistd.h>

#include <system_error>

#include "cleaner.hpp"
#include "pid_manager.hpp"

namespace fs = std::filesystem;

namespace dirclean {
namespace {

constexpr const char* kIdent = "rzaeva_dir_cleaner";
constexpr const char* kDefaultPidFile = "/tmp/rzaeva_dir_cleaner.pid";

fs::path pidFilePath() {
    if (const char* env = std::getenv("RZAEVA_PID_FILE"); env != nullptr && *env != '\0')
        return fs::path(env);
    return fs::path(kDefaultPidFile);
}

// SIGHUP и SIGTERM принимаются через sigtimedwait, а не обработчиками.
sigset_t handledSignals() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    return set;
}

}

Daemon& Daemon::instance() {
    static Daemon theInstance;
    return theInstance;
}

int Daemon::run(const std::string& configPath) {
    sigset_t signals = handledSignals();
    sigprocmask(SIG_BLOCK, &signals, nullptr);

    // Путь к конфигу делаем абсолютным, пока рабочий каталог ещё наш.
    std::error_code ec;
    configPath_ = fs::absolute(fs::path(configPath), ec).lexically_normal();
    if (ec) configPath_ = fs::path(configPath);

    openlog(kIdent, LOG_PID | LOG_NDELAY, LOG_DAEMON);

    // Конфиг проверяем до демонизации, чтобы при ошибке вернуть код.
    const ConfigParseResult pre = parseConfigFile(configPath_);
    if (!pre.ok) {
        syslog(LOG_ERR, "Cannot load config %s: %s", configPath_.c_str(), pre.error.c_str());
        closelog();
        return 1;
    }
    config_ = pre.config;
    intervalSeconds_ = pre.config.intervalSeconds > 0 ? pre.config.intervalSeconds : 1;

    PidManager pidFile(pidFilePath());
    const long previous = pidFile.stopPrevious();

    daemonize();

    if (previous > 0)
        syslog(LOG_NOTICE, "Found running instance (pid %ld), sent SIGTERM", previous);

    refreshConfig();

    pidFile.write(static_cast<long>(::getpid()));
    syslog(LOG_NOTICE, "Daemon started (pid %ld), config %s, interval %d s",
           static_cast<long>(::getpid()), configPath_.c_str(), intervalSeconds_);

    mainLoop();

    syslog(LOG_NOTICE, "Daemon stopped (pid %ld)", static_cast<long>(::getpid()));
    pidFile.remove();
    closelog();
    return 0;
}

void Daemon::refreshConfig() {
    const ConfigParseResult result = parseConfigFile(configPath_);

    for (const std::string& warning : result.warnings)
        syslog(LOG_WARNING, "%s", warning.c_str());

    if (!result.ok) {
        syslog(LOG_ERR, "Reload failed (%s), keeping previous rules", result.error.c_str());
        return;
    }

    config_ = result.config;
    intervalSeconds_ = config_.intervalSeconds > 0 ? config_.intervalSeconds : 1;
    syslog(LOG_INFO, "Config loaded: %zu rule(s), interval %d s from %s", config_.rules.size(),
           intervalSeconds_, configPath_.c_str());
}

void Daemon::daemonize() {
    const pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork failed: %s", std::strerror(errno));
        _exit(1);
    }
    if (pid > 0) _exit(0);

    if (setsid() < 0) {
        syslog(LOG_ERR, "setsid failed: %s", std::strerror(errno));
        _exit(1);
    }

    if (chdir("/") != 0) syslog(LOG_ERR, "chdir(\"/\") failed: %s", std::strerror(errno));

    const int nullFd = open("/dev/null", O_RDWR);
    if (nullFd >= 0) {
        dup2(nullFd, STDIN_FILENO);
        dup2(nullFd, STDOUT_FILENO);
        dup2(nullFd, STDERR_FILENO);
        if (nullFd > STDERR_FILENO) close(nullFd);
    }
}

void Daemon::mainLoop() {
    doWork();

    const sigset_t signals = handledSignals();
    for (;;) {
        timespec timeout{};
        timeout.tv_sec = intervalSeconds_;

        const int signal = sigtimedwait(&signals, nullptr, &timeout);
        if (signal < 0) {
            if (errno == EAGAIN) {
                doWork();
                continue;
            }
            if (errno == EINTR) continue;
            syslog(LOG_ERR, "sigtimedwait failed: %s", std::strerror(errno));
            break;
        }

        if (signal == SIGHUP) {
            syslog(LOG_NOTICE, "SIGHUP received, rereading config");
            refreshConfig();
        } else if (signal == SIGTERM) {
            syslog(LOG_NOTICE, "SIGTERM received, terminating");
            break;
        }
    }
}

void Daemon::doWork() {
    for (const Rule& rule : config_.rules) {
        std::error_code ec;
        if (!fs::is_directory(rule.folder, ec)) {
            syslog(LOG_WARNING, "%s is not a directory, skipping", rule.folder.c_str());
            continue;
        }

        if (hasIgnFile(rule.ignFile)) continue;

        try {
            const std::size_t removed = cleanFolderContents(rule.folder);
            syslog(LOG_NOTICE, "Cleaned %s (%zu entries removed)", rule.folder.c_str(), removed);
        } catch (const std::exception& ex) {
            syslog(LOG_ERR, "Failed to clean %s: %s", rule.folder.c_str(), ex.what());
        }
    }
}

}
