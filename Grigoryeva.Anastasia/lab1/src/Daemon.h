#pragma once

#include <csignal>
#include <ctime>
#include <map>
#include <string>
#include <vector>

#include "Event.h"

// Демон-напоминалка. Singleton: в системе существует ровно один экземпляр демона.
class Daemon {
public:
    static Daemon& instance();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    // Полный жизненный цикл: проверка pid-файла, демонизация, рабочий цикл.
    // Возвращает код выхода (вызывается только в итоговом процессе-демоне или при ошибке старта).
    int run(const std::string& configPath);

private:
    Daemon();
    ~Daemon() = default;

    struct Item {
        Event event;
        std::time_t next;  // ближайшее срабатывание или NO_OCCURRENCE
    };

    bool stopPreviousInstance();
    bool daemonize();
    bool writePidFile() const;
    void removePidFile() const;
    void installSignalHandlers() const;
    void reload();
    void tick();
    void mainLoop();
    void launchTerminal(const std::string& text);
    void reportError(const std::string& msg) const;

    static void onSignal(int sig);

    static volatile sig_atomic_t gotHup_;
    static volatile sig_atomic_t gotTerm_;

    std::string pidFile_;
    std::string configPath_;  // абсолютный путь, запоминается до смены рабочего каталога
    std::vector<Item> items_;
    std::map<std::string, std::time_t> handled_;  // ключ события -> момент, до которого оно уже обработано
};
