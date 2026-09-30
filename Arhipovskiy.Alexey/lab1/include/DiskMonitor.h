#ifndef DISKMON_DISK_MONITOR_H
#define DISKMON_DISK_MONITOR_H

#include "FileWatcher.h"
#include "PidFile.h"

#include <csignal>
#include <string>
#include <vector>

namespace diskmon {

// Singleton
class DiskMonitor {
public:
    static DiskMonitor& instance();

    DiskMonitor(const DiskMonitor&) = delete;
    DiskMonitor& operator=(const DiskMonitor&) = delete;
    DiskMonitor(DiskMonitor&&) = delete;
    DiskMonitor& operator=(DiskMonitor&&) = delete;

    [[nodiscard]] int run(const std::string& configFile);

private:
    DiskMonitor();
    ~DiskMonitor() = default;

    [[nodiscard]] bool loadConfig(const std::string& configFile);
    void reloadConfig();
    void stopRunningInstance() const;
    [[nodiscard]] static bool daemonize();
    void installSignalHandlers();
    [[nodiscard]] bool serve();

    std::string configPath_;
    std::vector<std::string> directories_;
    PidFile pidFile_;
    FileWatcher watcher_;
    sigset_t waitMask_{};
};

}

#endif // DISKMON_DISK_MONITOR_H
