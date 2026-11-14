#pragma once

#include <filesystem>
#include <string>

namespace diskmon {

struct Configuration;
class WatchManager;

struct CommandLineOptions {
    std::filesystem::path configPath = "disk-monitor.conf";
    std::filesystem::path pidPath = "/run/disk-monitor.pid";
    bool foreground = false;
};

class Daemon {
public:
    static Daemon& instance();
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    int run(const CommandLineOptions& options);

private:
    Daemon() = default;
    int runEventLoop(WatchManager& watchManager, const CommandLineOptions& options, Configuration& activeConfiguration);
};
} 
