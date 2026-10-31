#include "Daemon.hpp"

#include "Daemonize.hpp"
#include "Journal.hpp"
#include "PathUtil.hpp"
#include "ProcessInfo.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <optional>
#include <thread>

#include <poll.h>
#include <sys/signalfd.h>
#include <syslog.h>
#include <unistd.h>

namespace diskmond {

namespace {

constexpr auto kTerminationTimeout = std::chrono::seconds(5);
constexpr auto kTerminationPoll = std::chrono::milliseconds(50);

std::string withReason(const std::string& what, int code) {
    return what + ": " + std::strerror(code);
}

std::string counted(std::size_t count, const char* one, const char* many) {
    return std::to_string(count) + " " + (count == 1 ? one : many);
}

}  

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

int Daemon::run(const Options& options) {
    journal::open(journal::Echo::Stderr);
    if (!prepareLaunch(options)) {
        journal::close();
        return EXIT_FAILURE;
    }

    std::string error;
    StartupChannel channel;
    if (!channel.open(error)) {
        journal::status(LOG_ERR, "startup failed: " + error);
        journal::close();
        return EXIT_FAILURE;
    }

    journal::status(LOG_INFO,
                    "launching in background: config " + configPath_ + ", pid file " + pidFile_.location());
    const auto role = detachFromTerminal(channel, error);
    if (!role) {
        journal::status(LOG_ERR, "startup failed: " + error);
        journal::close();
        return EXIT_FAILURE;
    }
    if (*role == ProcessRole::Launcher) {
        journal::close();
        std::string message;
        const bool running = channel.awaitReport(message);
        (running ? std::cout : std::cerr) << "diskmond: " << message << std::endl;
        return running ? EXIT_SUCCESS : EXIT_FAILURE;
    }

    
    journal::close();
    const bool settled = settleAsDaemon(channel.daemonEnd(), error);
    journal::open(journal::Echo::Off);
    if (!settled || !bringUp(error)) {
        journal::status(LOG_ERR, "startup failed: " + error);
        channel.reportFailure(error);
        watcher_.close();
        pidFile_.release(::getpid());
        journal::close();
        return EXIT_FAILURE;
    }

    journal::status(LOG_NOTICE, "up and running as pid " + std::to_string(::getpid()) + ", " + coverage());
    channel.reportReady(::getpid());

    const bool clean = eventLoop();
    tearDown(clean);
    return clean ? EXIT_SUCCESS : EXIT_FAILURE;
}

bool Daemon::prepareLaunch(const Options& options) {
    
    const auto config = path::canonical(options.configFile);
    if (!config) {
        const int code = errno;
        journal::status(LOG_ERR, withReason("config '" + options.configFile + "' is not accessible", code));
        return false;
    }
    if (!path::isRegularFile(*config)) {
        journal::status(LOG_ERR, "config '" + *config + "' is not a regular file");
        return false;
    }
    configPath_ = *config;
    if (!readSettings()) {
        return false;
    }
    if (settings_.directories.empty()) {
        journal::status(LOG_WARNING, "nothing to watch: " + configPath_ +
                                         " lists no usable directories (fix it and send SIGHUP)");
    }

    pidFile_ = PidFile(options.pidFile.empty() ? defaultPidFileLocation() : path::absolute(options.pidFile));
    return replacePreviousInstance();
}

bool Daemon::readSettings() {
    std::string error;
    auto parsed = readSettingsFile(configPath_, error);
    if (!parsed) {
        journal::status(LOG_ERR, error);
        return false;
    }
    for (const auto& remark : parsed->remarks) {
        journal::status(LOG_WARNING, remark);
    }
    settings_ = std::move(parsed->settings);
    return true;
}

bool Daemon::replacePreviousInstance() {
    const auto previous = pidFile_.load();
    if (!previous || *previous == ::getpid()) {
        return true;
    }
    const std::string pidText = std::to_string(*previous);
    const std::string where = "pid file " + pidFile_.location() + " holds pid " + pidText;

    
    if (!process::isAlive(*previous)) {
        journal::status(LOG_INFO, where + ", but /proc/" + pidText + " is absent: the file is stale");
        return true;
    }
    
    if (!process::isSameProgram(*previous)) {
        journal::status(LOG_WARNING, where + ", which belongs to '" + process::name(*previous).value_or("?") +
                                         "', not to diskmond: not touching it");
        return true;
    }

    journal::status(LOG_INFO, "previous instance (pid " + pidText + ") is running: sending SIGTERM");
    if (::kill(*previous, SIGTERM) != 0) {
        const int code = errno;
        journal::status(LOG_ERR, withReason("cannot signal previous instance (pid " + pidText + ")", code));
        return false;
    }
    
    if (!awaitTermination(*previous)) {
        journal::status(LOG_ERR, "previous instance (pid " + pidText + ") is still alive after " +
                                     std::to_string(kTerminationTimeout.count()) + " s, giving up");
        return false;
    }
    journal::status(LOG_INFO, "previous instance (pid " + pidText + ") has exited");
    return true;
}

bool Daemon::awaitTermination(pid_t pid) const {
    const auto deadline = std::chrono::steady_clock::now() + kTerminationTimeout;
    while (process::isAlive(pid)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(kTerminationPoll);
    }
    return true;
}

bool Daemon::bringUp(std::string& error) {
    if (!prepareSignals(error)) {
        return false;
    }
    
    if (!pidFile_.store(::getpid(), error)) {
        return false;
    }
    if (!watcher_.open(error)) {
        return false;
    }
    watcher_.apply(settings_);
    journal::fileEvent("<start> " + coverage());
    return true;
}

bool Daemon::prepareSignals(std::string& error) {
    
    struct sigaction ignore {};
    ignore.sa_handler = SIG_IGN;
    sigemptyset(&ignore.sa_mask);
    ::sigaction(SIGPIPE, &ignore, nullptr);

    
    sigset_t handled;
    sigemptyset(&handled);
    sigaddset(&handled, SIGHUP);
    sigaddset(&handled, SIGTERM);
    if (::sigprocmask(SIG_BLOCK, &handled, nullptr) != 0) {
        const int code = errno;
        error = withReason("sigprocmask failed", code);
        return false;
    }
    signals_.reset(::signalfd(-1, &handled, SFD_NONBLOCK | SFD_CLOEXEC));
    if (!signals_.isOpen()) {
        const int code = errno;
        error = withReason("signalfd failed", code);
        return false;
    }
    return true;
}

bool Daemon::eventLoop() {
    while (!stopRequested_) {
        pollfd watched[] = {
            {signals_.get(), POLLIN, 0},
            {watcher_.fd(), POLLIN, 0},
        };
        
        const int timeout = watcher_.hasBacklog() ? 0 : -1;
        if (::poll(watched, 2, timeout) < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int code = errno;
            journal::status(LOG_ERR, withReason("poll failed", code));
            return false;
        }
        if ((watched[1].revents & POLLIN) || watcher_.hasBacklog()) {
            watcher_.consumeEvents();
        }
        if (watched[0].revents & POLLIN) {
            drainSignals();
        }
        if ((watched[0].revents | watched[1].revents) & (POLLERR | POLLNVAL)) {
            journal::status(LOG_ERR, "poll reported an error on a descriptor");
            return false;
        }
    }
    return true;
}

void Daemon::drainSignals() { 
    std::optional<pid_t> reloadRequestedBy;
    signalfd_siginfo info{};
    while (::read(signals_.get(), &info, sizeof info) == static_cast<ssize_t>(sizeof info)) {
        const auto sender = static_cast<pid_t>(info.ssi_pid);
        if (info.ssi_signo == SIGHUP) {
            reloadRequestedBy = sender;  // несколько SIGHUP подряд — одно перечитывание
        } else if (info.ssi_signo == SIGTERM) {
            journal::status(LOG_INFO, "SIGTERM from pid " + std::to_string(sender) + ": exiting");
            stopRequested_ = true;
        }
    }
    if (reloadRequestedBy && !stopRequested_) {
        rereadSettings(*reloadRequestedBy);
    }
}

void Daemon::rereadSettings(pid_t sender) {
    journal::status(LOG_INFO, "SIGHUP from pid " + std::to_string(sender) + ": rereading " + configPath_);
    if (!readSettings()) {
        journal::status(LOG_ERR, "reload aborted, the previous settings stay in effect");
        return;
    }
    if (settings_.directories.empty()) {
        journal::status(LOG_WARNING, "nothing to watch: " + configPath_ + " lists no usable directories");
    }
    watcher_.apply(settings_);
    journal::status(LOG_INFO, "reload done, " + coverage());
    journal::fileEvent("<reload> " + coverage());
}

void Daemon::tearDown(bool clean) {
    watcher_.consumeEvents();  
    watcher_.close();
    journal::fileEvent("<stop>");
    pidFile_.release(::getpid());
    journal::status(clean ? LOG_NOTICE : LOG_ERR,
                    clean ? "shutdown complete" : "shut down because of an error");
    journal::close();
}

std::string Daemon::coverage() const {
    return counted(watcher_.directoryCount(), "directory", "directories") + " under " +
           counted(watcher_.rootCount(), "root", "roots");
}

}  
