#pragma once

#include "Configuration.hpp"
#include "ReminderSchedule.hpp"
#include <csignal>
#include <string>

namespace reminder {
class PidFile;

// Single owner of the configuration, schedule and daemon lifecycle.
class Daemon final {
public:
    static Daemon& instance();
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    int run(const std::string& config, const std::string& pidfile);
private:
    Daemon() = default;
    static void prepareProcess(PidFile& pid, int readiness);
    void reloadConfiguration();
    void runLoop(int signalFd, const sigset_t& originalMask);

    std::string configPath_;
    Configuration settings_;
    ReminderSchedule schedule_;
};
}
