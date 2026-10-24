#pragma once

#include "events.h"

#include <map>
#include <string>
#include <chrono>
#include <sys/types.h>
#include <vector>

class Daemon {
public:
    static Daemon& instance();
    int run(const std::string& config, const std::string& pidFile, bool foreground);
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

private:
    Daemon() = default;
    ~Daemon();
    void readState();
    void saveState();
    void reload(bool initial);
    bool notify(const Event& event, std::time_t due);
    void checkNotifications();
    void reapChildren();
    void loop();
    void takePidFile();
    void writePid();

    std::string configPath;
    std::string pidPath;
    std::string executable;
    std::string terminal;
    Config config;
    std::map<std::string, std::time_t> delivered;
    int pidFd = -1;
    bool ownsPid = false;
    bool foreground = false;
    struct Notification {
        pid_t child;
        std::string key;
        std::time_t due;
        std::string acknowledgement;
        std::chrono::steady_clock::time_point started;
    };
    std::vector<Notification> pending;
};

std::string findTerminal(const std::string& requested);
