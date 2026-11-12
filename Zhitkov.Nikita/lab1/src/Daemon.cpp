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
#include <cerrno>
#include <fstream>
#include <stdexcept>
#include <thread>

namespace {

constexpr const char* kPidFile   = "/tmp/reminder_daemon.pid";
constexpr const char* kSyslogTag = "reminder_daemon";

volatile sig_atomic_t g_terminateFlag = 0;
volatile sig_atomic_t g_terminateSig  = 0;
volatile sig_atomic_t g_reloadFlag    = 0;

extern "C" void onSignal(int sig) {
    if (sig == SIGTERM || sig == SIGINT) {
        g_terminateSig  = sig;
        g_terminateFlag = 1;
    } else if (sig == SIGHUP) {
        g_reloadFlag = 1;
    }
}

bool isReminderDaemon(pid_t pid) {
    // /proc/<pid>/comm - имя процесса из ядра (task_struct->comm)
    // Буфер фиксированного размера TASK_COMM_LEN = 16 байт, из них под имя доступно 15 символов (последний байт - '\0')
    // Через argv[0] не подделать, в отличие от /proc/<pid>/cmdline
    const std::string commPath = "/proc/" + std::to_string(pid) + "/comm";
    std::ifstream comm(commPath);
    if (!comm) return false;
    std::string name;
    std::getline(comm, name);
    return name == "reminder_daemon";
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
    // Проверяем сам pid-файл: обычный файл, наш по владельцу, не writable для группы/остальных
    struct stat fst{};
    if (stat(pidFilePath_.c_str(), &fst) != 0) return;   // файла нет
    if (!S_ISREG(fst.st_mode)) {
        syslog(LOG_WARNING, "Pid file %s is not a regular file, ignoring",
               pidFilePath_.c_str());
        return;
    }
    if (fst.st_uid != getuid()) {
        syslog(LOG_WARNING, "Pid file %s owned by uid %d, not us, ignoring",
               pidFilePath_.c_str(), (int)fst.st_uid);
        return;
    }

    std::ifstream in(pidFilePath_);
    pid_t pid = 0;
    if (!(in >> pid) || pid <= 0 || pid == getpid()) return;

    // Проверяем, что это действительно наш демон, а не чужой процесс
    if (!isReminderDaemon(pid)) {
        syslog(LOG_WARNING, "Pid %d from %s is not reminder_daemon, ignoring",
               (int)pid, pidFilePath_.c_str());
        return;
    }

    syslog(LOG_WARNING, "Found running daemon (pid=%d), sending SIGTERM", (int)pid);
    kill(pid, SIGTERM);

    const std::string procPath = "/proc/" + std::to_string(pid);
    struct stat st{};
    for (int i = 0; i < 50; ++i) {
        if (stat(procPath.c_str(), &st) != 0) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    syslog(LOG_WARNING, "Old daemon (pid=%d) did not exit, SIGKILL", (int)pid);
    kill(pid, SIGKILL);
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
}

void Daemon::writePidFile() {
    int fd = open(pidFilePath_.c_str(),
                  O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0644);
    if (fd < 0) {
        syslog(LOG_ERR, "Cannot write pid file %s: %s",
               pidFilePath_.c_str(), std::strerror(errno));
        return;
    }
    // Принудительно 0644, даже если файл уже существовал с более слабыми правами
    fchmod(fd, 0644);
    dprintf(fd, "%d\n", (int)getpid());
    close(fd);
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
        // Терминация - наивысший приоритет, никогда не теряется
        if (g_terminateFlag) {
            syslog(LOG_INFO, "Received signal %d, shutting down",
                   (int)g_terminateSig);
            running_.store(false);
            break;
        }
        if (g_reloadFlag) {
            g_reloadFlag = 0;
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
            if (g_terminateFlag || g_reloadFlag) break;
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

        std::time_t due = 0;
        switch (decideEventAction(ev, last, now, grace, &due)) {
            case EventAction::Skip:
                break;

            case EventAction::MarkStale:
                syslog(LOG_INFO,
                       "Skipping stale event (missed by %ld sec): %s",
                       static_cast<long>(now - due), ev.text.c_str());
                // Привязка к расписанию, а не к текущему моменту
                fired_[key] = due;
                break;

            case EventAction::Fire:
                fireEvent(ev);
                fired_[key] = due;
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
               "DISPLAY не задан; нельзя открыть терминал. Сообщение: %s",
               text.c_str());
        return;
    }

    std::string esc;
    esc.reserve(text.size());
    for (char c : text) {
        if (c == '\\' || c == '"' || c == '`' || c == '$') esc += '\\';
        esc += c;
    }

    const std::string innerCmd =
        std::string("echo 'Напоминалка'; echo; echo \"") + esc +
        "\"; stty -echo; exec sleep infinity";

    // Каждый кандидат запускается через "sh -c" и передаёт код возврата
    // При ненулевом exit-коде || переходит к следующему
    // systemd-run без --wait падает сразу, если нет user bus
    const std::string script =
        "systemd-run --user --collect --quiet gnome-terminal -- "
            "/bin/sh -c \"$1\" 2>/dev/null || "
        "gnome-terminal -- /bin/sh -c \"$1\" 2>/dev/null || "
        "konsole -e /bin/sh -c \"$1\" 2>/dev/null || "
        "xfce4-terminal -e /bin/sh -c \"$1\" 2>/dev/null || "
        "mate-terminal -e /bin/sh -c \"$1\" 2>/dev/null || "
        "xterm -e /bin/sh -c \"$1\" 2>/dev/null || "
        "zenity --info --text \"$2\" 2>/dev/null";

    const pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork() failed while opening terminal");
        return;
    }
    if (pid == 0) {
        // $1 = innerCmd (что выполнить в терминале), $2 = text (для zenity)
        execlp("sh", "sh", "-c", script.c_str(), "sh",
               innerCmd.c_str(), text.c_str(), nullptr);
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