#include "disk_monitor/Daemon.hpp"

#include "disk_monitor/Config.hpp"
#include "disk_monitor/Logging.hpp"
#include "disk_monitor/ProcessControl.hpp"
#include "disk_monitor/WatchManager.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <poll.h>
#include <signal.h>
#include <vector>
#include <pthread.h>
#include <string>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>
#include <utility>

namespace diskmon {
namespace {

volatile sig_atomic_t stopRequested = 0;
volatile sig_atomic_t reloadRequested = 0;

void signalHandler(int signalNumber) {
    if (signalNumber == SIGHUP) {
        reloadRequested = 1;
    } else if (signalNumber == SIGTERM || signalNumber == SIGINT) {
        stopRequested = 1;
    }
}

bool installSignalHandlers(std::string& error) {
    struct sigaction action {};
    action.sa_handler = signalHandler;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (::sigaction(SIGHUP, &action, nullptr) != 0) {
        error = "sigaction(SIGHUP) failed: " + std::string(std::strerror(errno));
        return false;
    }
    if (::sigaction(SIGTERM, &action, nullptr) != 0) {
        error = "sigaction(SIGTERM) failed: " + std::string(std::strerror(errno));
        return false;
    }
    if (::sigaction(SIGINT, &action, nullptr) != 0) {
        error = "sigaction(SIGINT) failed: " + std::string(std::strerror(errno));
        return false;
    }
    struct sigaction pipeAction {};
    pipeAction.sa_handler = SIG_IGN;
    ::sigemptyset(&pipeAction.sa_mask);
    if (::sigaction(SIGPIPE, &pipeAction, nullptr) != 0) {
        error = "sigaction(SIGPIPE) failed: " + std::string(std::strerror(errno));
        return false;
    }
    return true;
}

bool takeReloadRequest() {
    sigset_t blockSet;
    sigset_t previousSet;
    ::sigemptyset(&blockSet);
    ::sigaddset(&blockSet, SIGHUP);
    (void)::sigprocmask(SIG_BLOCK, &blockSet, &previousSet);
    const bool requested = reloadRequested != 0;
    reloadRequested = 0;
    (void)::sigprocmask(SIG_SETMASK, &previousSet, nullptr);
    return requested;
}

bool isStopRequested() {
    return stopRequested != 0;
}

bool normalizeOptions(CommandLineOptions& options, std::string& error) {
    try {
        options.configPath = absoluteNormalizedPath(options.configPath);
        options.pidPath = absoluteNormalizedPath(options.pidPath);
    } catch (const std::exception& exception) {
        error = "cannot normalize command-line paths: " + std::string(exception.what());
        return false;
    }
    if (options.configPath.empty() || options.pidPath.empty()) {
        error = "configuration path and PID path must not be empty";
        return false;
    }
    return true;
}

void printError(const std::string& error) {
    const auto writeAll = [](const char* data, std::size_t size) {
        while (size > 0U) {
            const ssize_t written = ::write(STDERR_FILENO, data, size);

            if (written > 0) {
                const auto count = static_cast<std::size_t>(written);
                data += count;
                size -= count;
                continue;
            }

            if (written < 0 && errno == EINTR) {
                continue;
            }

            break;
        }
    };

    writeAll(error.data(), error.size());
    writeAll("\n", 1U);
}

}  // namespace

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

int Daemon::run(const CommandLineOptions& suppliedOptions) {
    CommandLineOptions options = suppliedOptions;
    initializeLogging(options.foreground);

    const auto startupFailure = [](int code, const std::string& message) {
        logDaemon(LOG_ERR, message);
        // These failures happen before the launcher has detached from its
        // terminal, so preserve a direct diagnostic as well as syslog.
        printError(message);
        closeLogging();
        return code;
    };

    std::string error;
    if (!normalizeOptions(options, error)) {
        return startupFailure(2, error);
    }

    ConfigLoadResult loadedConfig = loadConfiguration(options.configPath);
    if (!loadedConfig.configuration) {
        return startupFailure(2, loadedConfig.error);
    }

    ProcessLock processLock;
    if (!processLock.acquire(options.pidPath, error)) {
        return startupFailure(3, error);
    }

    int startupPipe = -1;
    if (!options.foreground) {
        DaemonizeResult daemonizeResult = daemonizeProcess();
        if (daemonizeResult.role == DaemonizeResult::Role::Error) {
            error = daemonizeResult.error;
            processLock.closeDescriptor();
            return startupFailure(4, error);
        }
        if (daemonizeResult.role == DaemonizeResult::Role::Parent) {
            processLock.closeDescriptor();
            const bool startupSucceeded = waitForDaemonStartup(daemonizeResult.statusPipe, error);
            std::string reapError;
            if (!reapDaemonizationChild(daemonizeResult.launcherChild, reapError) && startupSucceeded) {
                logDaemon(LOG_ERR, reapError);
                printError(reapError);
                closeLogging();
                return 4;
            }
            if (!startupSucceeded) {
                logDaemon(LOG_ERR, error);
                printError(error);
                closeLogging();
                return 4;
            }
            closeLogging();
            return 0;
        }
        startupPipe = daemonizeResult.statusPipe;
    }

    logDaemon(LOG_INFO, "disk-monitor initialization started; config=" + options.configPath.string());
    for (const auto& warning : loadedConfig.configuration->warnings) {
        logDaemon(LOG_WARNING, warning);
    }

    PidFile pidFile(options.pidPath);
    if (!options.foreground && startupPipe < 0) {
        logDaemon(LOG_ERR, "internal startup handshake was not initialized");
        closeLogging();
        return 4;
    }

    WatchManager watchManager;
    if (!watchManager.initialize()) {
        if (options.foreground) {
            printError("failed to initialize inotify; see the system journal for details");
        }
        if (startupPipe >= 0) {
            (void)reportDaemonStartup(startupPipe, false);
        }
        closeLogging();
        return 5;
    }
    if (!watchManager.applyRoots(loadedConfig.configuration->watchDirectories,
                                  loadedConfig.configuration->excludePaths)) {
        logDaemon(LOG_ERR, "failed to establish watches or recovery anchors for all configured roots; startup aborted");
        if (options.foreground) {
            printError("failed to establish watches or recovery anchors for all configured roots; see the system journal");
        }
        if (startupPipe >= 0) {
            (void)reportDaemonStartup(startupPipe, false);
        }
        closeLogging();
        return 5;
    }

    stopRequested = 0;
    reloadRequested = 0;
    if (!installSignalHandlers(error)) {
        logDaemon(LOG_ERR, error);
        if (options.foreground) {
            printError(error);
        }
        if (startupPipe >= 0) {
            (void)reportDaemonStartup(startupPipe, false);
        }
        closeLogging();
        return 5;
    }

    if (!pidFile.writeCurrentPid(error)) {
        logDaemon(LOG_ERR, error);
        if (options.foreground) {
            printError(error);
        }
        if (startupPipe >= 0) {
            (void)reportDaemonStartup(startupPipe, false);
        }
        closeLogging();
        return 5;
    }

    logDaemon(LOG_INFO, "disk-monitor started; pid=" + std::to_string(::getpid()) +
                        "; watched_roots=" + std::to_string(loadedConfig.configuration->watchDirectories.size()));

    if (startupPipe >= 0 && !reportDaemonStartup(startupPipe, true)) {
        logDaemon(LOG_WARNING, "could not report daemon startup status to launcher");
    }

    const int status = runEventLoop(watchManager, options, *loadedConfig.configuration);
    logDaemon(LOG_INFO, "disk-monitor is shutting down; pid=" + std::to_string(::getpid()));
    std::string cleanupError;
    if (!pidFile.removeIfOwned(cleanupError)) {
        logDaemon(LOG_ERR, cleanupError);
    }
    closeLogging();
    return status;
}

int Daemon::runEventLoop(WatchManager& watchManager,
                         const CommandLineOptions& options,
                         Configuration& activeConfiguration) {
    sigset_t handledSignals;
    sigset_t previousMask;
    sigset_t waitMask;
    ::sigemptyset(&handledSignals);
    ::sigaddset(&handledSignals, SIGHUP);
    ::sigaddset(&handledSignals, SIGTERM);
    ::sigaddset(&handledSignals, SIGINT);
    if (::sigprocmask(SIG_BLOCK, &handledSignals, &previousMask) != 0) {
        logDaemon(LOG_ERR, "cannot block control signals before ppoll: " + std::string(std::strerror(errno)));
        return 1;
    }
    waitMask = previousMask;
    ::sigdelset(&waitMask, SIGHUP);
    ::sigdelset(&waitMask, SIGTERM);
    ::sigdelset(&waitMask, SIGINT);

    int loopStatus = 0;
    while (!isStopRequested()) {
        if (takeReloadRequest()) {
            ConfigLoadResult replacement = loadConfiguration(options.configPath);
            if (!replacement.configuration) {
                logDaemon(LOG_ERR, "configuration reload rejected: " + replacement.error);
            } else {
                for (const auto& warning : replacement.configuration->warnings) {
                    logDaemon(LOG_WARNING, warning);
                }
                if (watchManager.applyRoots(replacement.configuration->watchDirectories,
                                            replacement.configuration->excludePaths)) {
                    activeConfiguration = std::move(*replacement.configuration);
                    logDaemon(LOG_INFO, "configuration reloaded; watched_roots=" +
                                        std::to_string(activeConfiguration.watchDirectories.size()));
                } else {
                    logDaemon(LOG_ERR, "configuration reload could not establish the requested root watches; previous watches retained");
                }
            }
        }
        if (isStopRequested()) {
            break;
        }

        struct pollfd descriptor {};
        descriptor.fd = watchManager.descriptor();
        descriptor.events = POLLIN;
        descriptor.revents = 0;

        const int pollResult = ::ppoll(&descriptor, 1, nullptr, &waitMask);
        if (pollResult < 0 && errno != EINTR) {
            logDaemon(LOG_ERR, "ppoll on inotify descriptor failed: " + std::string(std::strerror(errno)));
            loopStatus = 1;
            break;
        }
        if (pollResult > 0) {
            if ((descriptor.revents & POLLIN) != 0) {
                watchManager.processAvailableEvents();
            }
            if ((descriptor.revents & (POLLERR | POLLHUP | POLLNVAL)) != 0) {
                logDaemon(LOG_ERR, "ppoll reported an error on the inotify descriptor; attempting recovery");
                (void)watchManager.reconcile();
            }
        }
    }

    if (::sigprocmask(SIG_SETMASK, &previousMask, nullptr) != 0) {
        logDaemon(LOG_ERR, "cannot restore signal mask after event loop: " + std::string(std::strerror(errno)));
        return 1;
    }
    return loopStatus;
}
}  
