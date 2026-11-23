#pragma once

#include "Config.h"

#include <ctime>
#include <filesystem>
#include <string>

class ReminderDaemon final {
public:
    static ReminderDaemon& instance();

    ReminderDaemon(const ReminderDaemon&) = delete;
    ReminderDaemon& operator=(const ReminderDaemon&) = delete;

    // Родитель сразу возвращается, а демон продолжает работу внуком.
    int start(const std::filesystem::path& configFile);

private:
    ReminderDaemon() = default;
    ~ReminderDaemon();

    std::filesystem::path configPath_;
    std::filesystem::path pidPath_;
    Configuration config_;
    int pidFd_ = -1;

    void acquirePidFile();
    void writePid() const;
    void clearPid();
    void installSignalHandlers() const;
    void reloadConfiguration();
    void runLoop();
    void processReminders(std::time_t now);
    void reapTerminalProcesses() const;

    bool alreadyFired(const ReminderEvent& event, std::time_t due) const;
    bool rememberFiring(const ReminderEvent& event, std::time_t due) const;
    bool openTerminal(const std::string& text) const;
};
