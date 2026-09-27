#pragma once

#include <string>
#include <csignal>

namespace monitor {

class Daemon {
public:
    static Daemon& getInstance();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    void init(const std::string& configPath);

    void run();

private:
    Daemon() = default;
    ~Daemon() = default;

    void daemonize();
    void checkAndKillExisting();
    void writePidFile();
    void removePidFile();
    void setupSignals();

    std::string configPath_;
    std::string pidFilePath_ = "/tmp/disk_monitor.pid";
};

} // namespace monitor