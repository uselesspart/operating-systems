#pragma once

#include <csignal>
#include <filesystem>
#include <string>
#include <vector>

struct FolderRule {
    std::filesystem::path folder;
    std::string ignoreFile;
};

class Daemon {
public:
    static Daemon& instance();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    void start(const std::filesystem::path& configPath);

private:
    Daemon() = default;

    static volatile sig_atomic_t reloadRequested_;
    static volatile sig_atomic_t terminateRequested_;

    std::filesystem::path configPath_;
    std::filesystem::path pidFilePath_;
    std::vector<FolderRule> rules_;
    int intervalSeconds_ = 20;

    static void handleSignal(int signalNumber);

    void stopPreviousInstance();
    void daemonize();
    void installSignalHandlers();
    void writePidFile();
    void removePidFile() const;
    void loadConfig();
    void processFolders() const;
    void runLoop();
};
