#ifndef LAB1_DAEMON_H
#define LAB1_DAEMON_H

#include <sys/types.h>

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

#include "common.h"
#include "config.h"

namespace lab1 {

/// Background process performing the configured action over the configured
/// folders once every kScanIntervalSeconds seconds.
///
/// The class is a singleton: a daemon exists in exactly one instance per host,
/// and the class itself guards against a second one being created.
class Daemon {
public:
    /// Returns the only instance of the daemon.
    static Daemon& instance();

    /// Starts the daemon and does not return until it is terminated.
    RunResult run(const std::string& configPath);

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    Daemon& operator=(Daemon&&) = delete;

private:
    Daemon() = default;

    bool resolveConfigPath(const std::string& configPath);
    bool stopPreviousInstance();
    bool daemonize();
    bool writePidFile();
    void removePidFile();
    void installSignalHandlers();
    void mainLoop();
    void reloadConfig();
    void performActions();
    void processRule(const Rule& rule) const;

    using Clock = std::chrono::steady_clock;

    Config config_;
    std::vector<Clock::time_point> nextRun_;
    std::string configPath_;
    std::string executablePath_;
    pid_t pid_ = 0;
    int startupStatusFd_ = -1;
};

}  // namespace lab1

#endif  // LAB1_DAEMON_H
