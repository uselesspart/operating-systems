
#pragma once

#include "Config.h"
#include "Logger.h"
#include "PidFile.h"
#include "SafeCleaner.h"

#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

class Daemon {
public:
    struct Options {
        std::filesystem::path configPath{"config.conf"};
        std::filesystem::path pidPath{"/tmp/daemon_lab1.pid"};
        bool foreground = false;
        bool help = false;
    };

    struct LogLine {
        Logger::Level level;
        std::string text;
    };

    static Daemon& instance();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    static bool parseArguments(const std::vector<std::string>& args, Options& options,
                               std::string& error);
    static std::string usage(const std::string& program);
    static std::filesystem::path resolvePath(const std::filesystem::path& path,
                                             std::string& error);
    static std::vector<LogLine> describe(const SafeCleaner::Report& report);

    int run(const Options& options);

private:
    Daemon() = default;

    int runImpl(const Options& options);
    bool prepareConfiguration(Config& config, std::unique_ptr<SafeCleaner>& cleaner);
    bool daemonize(int& exitCode);
    bool becomeDaemon(int& exitCode);
    void reportStartup(bool ok, const std::string& message);
    int loop();
    void runCycle();
    bool reload();
    std::string summary() const;

    std::filesystem::path configPath_;
    std::filesystem::path pidPath_;
    bool foreground_ = false;
    int startupFd_ = -1;
    Config config_;
    std::unique_ptr<SafeCleaner> cleaner_;
    std::unique_ptr<PidFile> pidFile_;
    std::map<std::string, SafeCleaner::Outcome> lastOutcome_;
};
