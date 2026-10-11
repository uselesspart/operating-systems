#pragma once

#include <atomic>
#include <ctime>
#include <map>
#include <string>
#include <tuple>
#include <vector>

#include "Event.h"

class Daemon {
public:
    static Daemon& instance();

    // Возвращает true при нормальном завершении, false при фатальной ошибке
    bool start(const std::string& configPath);

    Daemon(const Daemon&)            = delete;
    Daemon& operator=(const Daemon&) = delete;

private:
    Daemon() = default;
    ~Daemon() = default;

    // Уникальный ключ события для сохранения состояния между перезагрузками
    using EventKey = std::tuple<std::time_t, std::string>;

    void daemonize();
    void setupSignalHandlers();
    void checkAndKillExisting();
    void writePidFile();
    void loadConfig();
    void runLoop();
    void checkEvents();
    void fireEvent(const Event& ev);
    void openTerminal(const std::string& text) const;
    void cleanup();

    std::string configPath_;
    std::string pidFilePath_;
    int         intervalSeconds_ = 10;
    std::string eventsFilePath_;

    // lastFired по событиям: сохраняется между итерациями и перезагрузками
    std::map<EventKey, std::time_t> fired_;

    std::atomic<bool> running_{false};
};