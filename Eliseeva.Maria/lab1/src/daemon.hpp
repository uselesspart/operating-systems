#pragma once

#include "config.hpp"

#include <cstddef>
#include <string>

namespace dirclean {

class PidManager;

class Daemon {
public:
    static Daemon &instance();

    Daemon(const Daemon &) = delete;
    Daemon &operator=(const Daemon &) = delete;
    Daemon(Daemon &&) = delete;
    Daemon &operator=(Daemon &&) = delete;

    // Полный жизненный цикл: конфиг, защита от повторного запуска, демонизация,
    // главный цикл. Возвращает код выхода только при ошибке до демонизации.
    int run(const std::string &configArg);

    // Запомнить абсолютный путь к конфигу. false, если файла нет.
    bool setConfigPath(const std::string &path);
    const std::string &configPath() const { return configPath_; }

    // Перечитать конфиг. При ошибке остаётся прежняя конфигурация.
    bool reloadConfig();
    const Config &config() const { return config_; }

    // Один проход по всем правилам. Возвращает число очищенных папок.
    std::size_t processRules();

    // Путь pid-файла: $DIRCLEAN_PID_FILE или /tmp/dirclean_daemon.pid.
    static std::string defaultPidFilePath();

private:
    Daemon() = default;

    void daemonize();
    void mainLoop(const PidManager &pidManager);

    std::string configPath_;
    Config config_;
};

} 
