#include "Daemon.hpp"
#include "ConfigLoader.hpp"
#include "FileDescriptor.hpp"
#include "PidFile.hpp"
#include "ReminderSchedule.hpp"
#include "TerminalNotifier.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>
#include <utility>

namespace reminder {
namespace {
void failIf(bool condition, const char* operation) {
    if (condition) throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

}

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

void Daemon::prepareProcess(PidFile& pid, int readiness) {
    failIf(setsid() < 0, "setsid");
    const pid_t second = fork();
    failIf(second < 0, "Second fork");
    if (second > 0) _exit(0);
    umask(0077);
    failIf(chdir("/") < 0, "chdir");
    // Close inherited descriptors, preserving the PID lock and startup reply.
    pid.releaseLaunchLock();
    closelog();
    struct rlimit limit{};
    failIf(getrlimit(RLIMIT_NOFILE, &limit) < 0, "getrlimit");
    const auto maximum = limit.rlim_cur == RLIM_INFINITY ? 1048576 : limit.rlim_cur;
    for (rlim_t fd = 0; fd < maximum; ++fd) {
        if (static_cast<int>(fd) != pid.descriptor() && static_cast<int>(fd) != readiness) close(static_cast<int>(fd));
    }
    const int null = open("/dev/null", O_RDWR);
    failIf(null < 0, "Open /dev/null");
    for (int fd = 0; fd < 3; ++fd) failIf(dup2(null, fd) < 0, "Redirect standard stream");
    if (null > 2) close(null);
    openlog("roman-reminder", LOG_PID | LOG_NDELAY, LOG_DAEMON);
}

void Daemon::reloadConfiguration() {
    try {
        auto updated = ConfigLoader::load(configPath_);
        schedule_.reload(updated, std::time(nullptr));
        settings_ = std::move(updated);
        syslog(LOG_INFO, "Configuration reloaded");
    } catch (const std::exception& error) {
        syslog(LOG_ERR, "Reload rejected, keeping previous settings: %s", error.what());
    }
}

void Daemon::runLoop(int signalFd, const sigset_t& originalMask) {
    bool running = true;
    while (running) {
        TerminalNotifier notifier(settings_.terminal);
        for (const auto& event : schedule_.takeDue(std::time(nullptr))) {
            notifier.show(event.message, originalMask);
        }
        pollfd watch{signalFd, POLLIN, 0};
        const int ready = poll(&watch, 1, static_cast<int>(settings_.interval * 1000));
        failIf(ready < 0 && errno != EINTR, "poll");
        signalfd_siginfo info{};
        while (read(signalFd, &info, sizeof(info)) == static_cast<ssize_t>(sizeof(info))) {
            if (info.ssi_signo == SIGTERM || info.ssi_signo == SIGINT) running = false;
            if (info.ssi_signo == SIGHUP) reloadConfiguration();
        }
        TerminalNotifier::reapChildren();
    }
}

int Daemon::run(const std::string& config, const std::string& pidfile) {
    openlog("roman-reminder", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    int readiness = -1;
    bool detached = false;
    try {
        configPath_ = config;
        settings_ = ConfigLoader::load(configPath_);
        PidFile pid(pidfile);
        sigset_t signals, originalMask;
        sigemptyset(&signals);
        for (int signal : {SIGHUP, SIGTERM, SIGINT, SIGCHLD}) sigaddset(&signals, signal);
        failIf(sigprocmask(SIG_BLOCK, &signals, &originalMask) < 0, "Block signals");
        int channel[2];
        failIf(pipe2(channel, O_CLOEXEC) < 0, "Startup pipe");
        const pid_t first = fork();
        failIf(first < 0, "Fork");
        if (first > 0) {
            close(channel[1]);
            char status = 0;
            ssize_t count;
            do { count = read(channel[0], &status, 1); } while (count < 0 && errno == EINTR);
            close(channel[0]);
            while (waitpid(first, nullptr, 0) < 0 && errno == EINTR) {}
            sigprocmask(SIG_SETMASK, &originalMask, nullptr);
            closelog();
            return count == 1 && status == '1' ? 0 : 1;
        }
        close(channel[0]);
        detached = true;
        readiness = channel[1];
        prepareProcess(pid, readiness);
        FileDescriptor signalFd(signalfd(-1, &signals, SFD_CLOEXEC | SFD_NONBLOCK));
        pid.writeCurrent();
        schedule_.reload(settings_, std::time(nullptr));
        syslog(LOG_INFO, "Started; config=%s", config.c_str());
        failIf(write(readiness, "1", 1) != 1, "Startup reply");
        close(readiness);
        readiness = -1;
        runLoop(signalFd.get(), originalMask);
        syslog(LOG_INFO, "Stopping after termination signal");
        pid.clear();
        closelog();
        _exit(0);
    } catch (const std::exception& error) {
        syslog(LOG_ERR, "%s", error.what());
        if (readiness >= 0) {
            ssize_t written;
            do {
                written = write(readiness, "0", 1);
            } while (written < 0 && errno == EINTR);
            if (written != 1) {
                syslog(LOG_ERR, "Cannot send startup failure reply: %s",
                       written < 0 ? std::strerror(errno) : "short write");
            }
            close(readiness);
        }
        closelog();
        if (detached) _exit(1);
        throw;
    }
}
}
