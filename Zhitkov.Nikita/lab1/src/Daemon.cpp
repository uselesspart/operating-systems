#include "Daemon.h"
#include "Config.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {

constexpr const char* kPidFile   = "/tmp/reminder_daemon.pid";
constexpr const char* kSyslogTag = "reminder_daemon";

// Единственный флаг, который выставляется в обработчике сигнала
volatile sig_atomic_t g_pendingSignal = 0;

extern "C" void onSignal(int sig) {
    g_pendingSignal = sig;
}

}

Daemon& Daemon::instance() {
    static Daemon inst;
    return inst;
}

bool Daemon::start(const std::string& configPath) {
    // Абсолютный путь к конфигу (запоминается на всё время работы)
    char resolved[PATH_MAX];
    if (realpath(configPath.c_str(), resolved) == nullptr) {
        std::fprintf(stderr, "Cannot resolve config path: %s\n", configPath.c_str());
        return false;
    }
    configPath_ = resolved;

    // Проверка pid-файла и убийство старого демона
    pidFilePath_ = kPidFile;
    checkAndKillExisting();

    // Демонизация
    daemonize();

    openlog(kSyslogTag, LOG_PID | LOG_NDELAY, LOG_DAEMON);
    syslog(LOG_INFO, "Daemon started (config=%s)", configPath_.c_str());

    // Запись pid-файла уже от демонизированного процесса
    writePidFile();

    // Обработчики сигналов
    setupSignalHandlers();

    // Загрузка конфига
    try {
        loadConfig();
    } catch (const std::exception& e) {
        syslog(LOG_ERR, "Config load failed: %s", e.what());
        cleanup();
        return false;
    }

    // Основной цикл
    running_.store(true);
    runLoop();

    cleanup();
    return true;
}

void Daemon::daemonize() {
    pid_t pid = fork();
    if (pid < 0) throw std::runtime_error("fork() failed");
    if (pid > 0) _exit(0);  // родитель завершается

    if (setsid() < 0) throw std::runtime_error("setsid() failed");

    // Второй fork - гарантия, что демон не станет ведущим процесса сессии
    pid = fork();
    if (pid < 0) throw std::runtime_error("second fork() failed");
    if (pid > 0) _exit(0);

    umask(0);
    if (chdir("/") < 0) throw std::runtime_error("chdir() failed");

    // Закрываем все файловые дескрипторы
    const long maxFd = sysconf(_SC_OPEN_MAX);
    for (long fd = maxFd; fd >= 0; --fd) {
        close(static_cast<int>(fd));
    }

    // Перенаправляем stdin/stdout/stderr в /dev/null
    const int devNull = open("/dev/null", O_RDWR);
    if (devNull >= 0) {
        dup2(devNull, STDIN_FILENO);
        dup2(devNull, STDOUT_FILENO);
        dup2(devNull, STDERR_FILENO);
        if (devNull > STDERR_FILENO) close(devNull);
    }
}

void Daemon::setupSignalHandlers() {
    struct sigaction sa{};
    sa.sa_handler = onSignal;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGHUP,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
    sigaction(SIGINT,  &sa, nullptr);

    // Дочерние процессы (открытие терминала) не должны превращаться в зомби
    struct sigaction saIgn{};
    saIgn.sa_handler = SIG_IGN;
    saIgn.sa_flags   = SA_NOCLDWAIT;
    sigemptyset(&saIgn.sa_mask);
    sigaction(SIGCHLD, &saIgn, nullptr);
}

void Daemon::checkAndKillExisting() {
    std::ifstream in(pidFilePath_);
    if (!in) return;

    pid_t pid = 0;
    if (!(in >> pid) || pid <= 0) return;

    const std::string procPath = "/proc/" + std::to_string(pid);
    struct stat st{};
    if (stat(procPath.c_str(), &st) != 0) {
        return;  // процесс уже не существует - pid-файл устарел
    }

    syslog(LOG_WARNING, "Found running daemon (pid=%d), sending SIGTERM", pid);
    kill(pid, SIGTERM);

    // Ждём завершения до 5 секунд
    for (int i = 0; i < 50; ++i) {
        if (stat(procPath.c_str(), &st) != 0) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    syslog(LOG_WARNING, "Old daemon (pid=%d) did not exit, SIGKILL", pid);
    kill(pid, SIGKILL);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void Daemon::writePidFile() {
    std::ofstream out(pidFilePath_, std::ios::trunc);
    if (!out) {
        syslog(LOG_ERR, "Cannot write pid file: %s", pidFilePath_.c_str());
        return;
    }
    out << getpid() << '\n';
}

void Daemon::loadConfig() {
    const Config cfg = parseConfig(configPath_);
    intervalSeconds_ = cfg.intervalSeconds;

    // Относительный путь events_file разрешаем относительно каталога конфига
    if (!cfg.eventsFile.empty() && cfg.eventsFile[0] != '/') {
        const auto slash = configPath_.find_last_of('/');
        const std::string dir = (slash == std::string::npos)
                                    ? std::string(".")
                                    : configPath_.substr(0, slash);
        eventsFilePath_ = dir + "/" + cfg.eventsFile;
    } else {
        eventsFilePath_ = cfg.eventsFile;
    }

    syslog(LOG_INFO, "Config loaded: interval=%d, events_file=%s",
           intervalSeconds_, eventsFilePath_.c_str());
}

void Daemon::runLoop() {
    while (running_.load()) {
        // Обработка отложенного сигнала
        const int sig = g_pendingSignal;
        g_pendingSignal = 0;

        if (sig == SIGTERM || sig == SIGINT) {
            syslog(LOG_INFO, "Received signal %d, shutting down", sig);
            running_.store(false);
            break;
        }
        if (sig == SIGHUP) {
            syslog(LOG_INFO, "SIGHUP received, reloading config");
            try {
                loadConfig();
            } catch (const std::exception& e) {
                syslog(LOG_ERR, "Config reload failed: %s", e.what());
            }
        }

        checkEvents();

        // Спим интервал долями по 1 секунде, чтобы быстро реагировать на сигналы
        for (int i = 0; i < intervalSeconds_ && running_.load(); ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            if (g_pendingSignal != 0) break;
        }
    }
}

void Daemon::checkEvents() {
    const std::time_t now   = std::time(nullptr);
    const std::time_t grace = intervalSeconds_;

    for (const auto& ev : parseEventsFile(eventsFilePath_)) {
        const EventKey key{ev.time, ev.text};

        std::time_t last = 0;
        const auto it = fired_.find(key);
        if (it != fired_.end()) last = it->second;

        switch (decideEventAction(ev, last, now, grace)) {
            case EventAction::Skip:
                break;

            case EventAction::MarkStale:
                syslog(LOG_INFO,
                       "Skipping stale event (missed by %ld sec): %s",
                       static_cast<long>(now - ev.time), ev.text.c_str());
                fired_[key] = now;
                break;

            case EventAction::Fire:
                fireEvent(ev);
                fired_[key] = now;
                break;
        }
    }
}

void Daemon::fireEvent(const Event& ev) {
    syslog(LOG_INFO, "Reminder fires: %s", ev.text.c_str());
    openTerminal(ev.text);
}

void Daemon::openTerminal(const std::string& text) const {
    const char* display = std::getenv("DISPLAY");
    if (display == nullptr || *display == '\0') {
        syslog(LOG_WARNING,
               "DISPLAY is not set; cannot open terminal. Message: %s",
               text.c_str());
        return;
    }

    // Экранируем опасные символы для передачи внутрь sh
    std::string esc;
    esc.reserve(text.size());
    for (char c : text) {
        if (c == '\\' || c == '"' || c == '`' || c == '$') esc += '\\';
        esc += c;
    }

    const std::string shellCmd =
        std::string("echo 'REMINDER'; echo \"") + esc +
        "\"; sleep 60";

    const pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork() failed while opening terminal");
        return;
    }

    if (pid == 0) {
        // Всё, что через GNOME-сервер - оборачиваем в systemd-run, чтобы получить правильный контекст сессии
        execlp("systemd-run", "systemd-run",
                "--user", "--collect",
                "gnome-terminal", "--",
                "/bin/sh", "-c", shellCmd.c_str(), nullptr);

        // Konsole с флагом --separate - работает без D-Bus
        execlp("konsole", "konsole", "--separate",
                "-e", "/bin/sh", "-c", shellCmd.c_str(), nullptr);

        // XFCE-терминал с флагом --disable-server
        execlp("xfce4-terminal", "xfce4-terminal", "--disable-server",
                "-e", "/bin/sh", "-c", shellCmd.c_str(), nullptr);

        // MATE - та же беда, что у GNOME, пусть попробует
        execlp("mate-terminal", "mate-terminal",
                "-e", "/bin/sh", "-c", shellCmd.c_str(), nullptr);

        // Одиночные терминалы - работают без всякой магии
        execlp("xterm", "xterm",
                "-e", "/bin/sh", "-c", shellCmd.c_str(), nullptr);
        execlp("alacritty", "alacritty",
                "-e", "/bin/sh", "-c", shellCmd.c_str(), nullptr);
        execlp("kitty", "kitty",
                "/bin/sh", "-c", shellCmd.c_str(), nullptr);

        // Универсальный fallback - если терминала нет совсем
        execlp("zenity", "zenity",
                "--info", "--title=Reminder", "--text", text.c_str(), nullptr);

        _exit(1);
    }
}

void Daemon::cleanup() {
    // Удаляем pid-файл только если он наш (иначе не трогаем файл нового демона)
    std::ifstream in(pidFilePath_);
    pid_t stored = 0;
    if ((in >> stored) && stored == getpid()) {
        in.close();
        unlink(pidFilePath_.c_str());
    }

    syslog(LOG_INFO, "Daemon (pid=%d) stopped", static_cast<int>(getpid()));
    closelog();
}