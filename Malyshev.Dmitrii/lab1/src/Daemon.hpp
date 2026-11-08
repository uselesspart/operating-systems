#pragma once

#include <csignal>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

struct inotify_event;

class Daemon {
public:
    static Daemon& instance();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    int run(const std::string& configPath);

private:
    Daemon() = default;
    ~Daemon() = default;

    bool loadConfig(std::string& error);
    bool stopPreviousInstance();
    bool daemonize();
    bool writePidFile();
    void removePidFile();
    void setupSignals();
    void mainLoop();

    bool setupWatches();
    void closeWatches();
    void addWatchTree(const std::filesystem::path& root);
    void addWatch(const std::filesystem::path& dir);
    void readEvents();
    void handleEvent(const struct inotify_event& ev);

    static void onSignal(int sig);

    std::string configPath_;
    std::vector<std::filesystem::path> folders_;
    bool recursive_ = true;

    int inotifyFd_ = -1;
    std::map<int, std::filesystem::path> watches_;

    static volatile std::sig_atomic_t gotTerm_;
    static volatile std::sig_atomic_t gotHup_;
};
