#pragma once
#include "config.hpp"
#include "pid_manager.hpp"

#include <string>

// Демон — синглтон: в системе он существует в единственном экземпляре.
class Daemon {
public:
    static Daemon& instance();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    // Полный жизненный цикл: разбор конфига, остановка старого экземпляра,
    // демонизация, главный цикл. Возвращает код выхода процесса.
    int run(const std::string& configPath);

private:
    Daemon();

    bool daemonize();
    int loop();
    void cleanup();
    bool reloadConfig();

    std::string configPath_;  // абсолютный путь, запоминается до chdir("/")
    Config config_;
    PidManager pid_;
};
