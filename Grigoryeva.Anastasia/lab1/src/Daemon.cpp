#include "Daemon.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <syslog.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>

namespace {

// Напоминание, опоздавшее не более чем на столько секунд, всё равно показывается
// (например, событие добавили в ту же минуту, на которую оно назначено).
const std::time_t kGraceSeconds = 60;
const int kStopWaitSteps = 50;  // 50 * 0.1 c = 5 секунд ожидания завершения старого демона

std::string readFirstLine(const std::string& path) {
    std::ifstream f(path);
    std::string s;
    std::getline(f, s);
    return s;
}

// Процесс существует (по данным /proc) и не является зомби.
bool isProcessAlive(pid_t pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    std::string s;
    if (!std::getline(f, s)) return false;
    std::size_t p = s.rfind(')');
    if (p == std::string::npos || p + 2 >= s.size()) return false;
    char state = s[p + 2];
    return state != 'Z' && state != 'X';
}

// Процесс с таким pid жив И это та же программа, что и мы (защита от устаревшего pid-файла,
// когда номер уже достался постороннему процессу).
bool isSameProgram(pid_t pid) {
    if (!isProcessAlive(pid)) return false;
    return readFirstLine("/proc/" + std::to_string(pid) + "/comm") == readFirstLine("/proc/self/comm");
}

std::string shellQuote(const std::string& s) {
    std::string r = "'";
    for (char c : s) {
        if (c == '\'') r += "'\\''";
        else r += c;
    }
    return r + "'";
}

std::string findInPath(const std::string& name) {
    if (name.find('/') != std::string::npos) return access(name.c_str(), X_OK) == 0 ? name : "";
    const char* path = std::getenv("PATH");
    if (!path) return "";
    std::stringstream ss(path);
    std::string dir;
    while (std::getline(ss, dir, ':')) {
        if (dir.empty()) continue;
        std::string full = dir + "/" + name;
        if (access(full.c_str(), X_OK) == 0) return full;
    }
    return "";
}

bool isWsl() {
    std::string v = readFirstLine("/proc/version");
    std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
    return v.find("microsoft") != std::string::npos;
}

std::vector<std::string> splitWords(const std::string& s) {
    std::istringstream in(s);
    std::vector<std::string> words;
    std::string w;
    while (in >> w) words.push_back(w);
    return words;
}

bool writeAll(int fd, const std::string& data) {
    std::size_t off = 0;
    while (off < data.size()) {
        ssize_t n = write(fd, data.data() + off, data.size() - off);
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        off += static_cast<std::size_t>(n);
    }
    return true;
}

// Shell-скрипт, который выполняется внутри нового терминала: печатает текст и ждёт Enter.
// Текст попадает в скрипт в одинарных кавычках, поэтому спецсимволы в нём безопасны.
std::string buildMessageScript(const std::string& text) {
    char stamp[32];
    std::time_t now = std::time(nullptr);
    std::tm t;
    localtime_r(&now, &t);
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", &t);

    std::string s;
    s += "#!/bin/sh\n";
    s += "trap 'rm -f \"$0\"' EXIT\n";
    s += "printf '\\n==============================\\n'\n";
    s += "printf '  НАПОМИНАНИЕ  (%s)\\n' '" + std::string(stamp) + "'\n";
    s += "printf '==============================\\n\\n'\n";
    s += "printf '%s\\n\\n' " + shellQuote(text) + "\n";
    s += "printf 'Нажмите Enter, чтобы закрыть окно... '\n";
    s += "read _answer\n";
    return s;
}

// Подбирает команду запуска нового терминала для скрипта scriptPath.
bool buildTerminalCommand(const std::string& scriptPath, std::vector<std::string>& args, bool& wsl) {
    wsl = false;

    // 1) Явное переопределение (используется в автотестах)
    const char* custom = std::getenv("REMINDERD_TERMINAL_CMD");
    if (custom && *custom) {
        args = splitWords(custom);
        if (args.empty()) return false;
        std::string exe = findInPath(args[0]);
        if (exe.empty()) return false;
        args[0] = exe;
        args.push_back(scriptPath);
        return true;
    }

    // 2) WSL: открываем окно консоли Windows, которое запускает wsl.exe
    if (isWsl()) {
        std::string cmdExe = findInPath("cmd.exe");
        if (cmdExe.empty() && access("/mnt/c/Windows/System32/cmd.exe", X_OK) == 0) {
            cmdExe = "/mnt/c/Windows/System32/cmd.exe";
        }
        if (!cmdExe.empty()) {
            args = {cmdExe, "/c", "start", "Reminder", "wsl.exe"};
            const char* distro = std::getenv("WSL_DISTRO_NAME");
            if (distro && *distro) {
                args.push_back("-d");
                args.push_back(distro);
            }
            args.push_back("-e");
            args.push_back("sh");
            args.push_back(scriptPath);
            wsl = true;
            return true;
        }
    }

    // 3) Обычный Linux с графической оболочкой
    if (std::getenv("DISPLAY") || std::getenv("WAYLAND_DISPLAY")) {
        struct Term {
            const char* name;
            const char* execFlag;
        };
        const Term terms[] = {{"x-terminal-emulator", "-e"}, {"gnome-terminal", "--"},
                              {"konsole", "-e"},             {"xfce4-terminal", "-x"},
                              {"xterm", "-e"}};
        for (const Term& term : terms) {
            std::string exe = findInPath(term.name);
            if (exe.empty()) continue;
            args = {exe, term.execFlag, "sh", scriptPath};
            return true;
        }
    }
    return false;
}

}  // namespace

volatile sig_atomic_t Daemon::gotHup_ = 0;
volatile sig_atomic_t Daemon::gotTerm_ = 0;

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

Daemon::Daemon() {
    const char* env = std::getenv("REMINDERD_PIDFILE");
    pidFile_ = (env && *env) ? env : "/tmp/reminderd_" + std::to_string(getuid()) + ".pid";
}

void Daemon::reportError(const std::string& msg) const {
    syslog(LOG_ERR, "%s", msg.c_str());
    std::cerr << "reminderd: " << msg << std::endl;
}

int Daemon::run(const std::string& configPath) {
    openlog("reminderd", LOG_PID, LOG_DAEMON);

    // Запоминаем абсолютный путь ДО того, как демон сменит рабочий каталог на "/"
    char* abs = realpath(configPath.c_str(), nullptr);
    if (!abs) {
        reportError("config file '" + configPath + "' not found: " + std::strerror(errno));
        closelog();
        return 1;
    }
    configPath_ = abs;
    std::free(abs);

    {
        std::vector<Event> probe;
        std::vector<std::string> errors;
        if (!loadEventsFromFile(configPath_, probe, errors)) {
            reportError(errors.empty() ? "cannot read config" : errors[0]);
            closelog();
            return 1;
        }
    }

    if (!stopPreviousInstance()) {
        closelog();
        return 1;
    }
    if (!daemonize()) {
        closelog();
        return 1;
    }

    // Мы - уже итоговый процесс-демон. Дескрипторы закрыты, поэтому открываем журнал заново.
    openlog("reminderd", LOG_PID, LOG_DAEMON);
    installSignalHandlers();
    if (!writePidFile()) {
        reportError("cannot write pid file " + pidFile_);
        closelog();
        return 1;
    }

    syslog(LOG_INFO, "started, pid=%d, config=%s", static_cast<int>(getpid()), configPath_.c_str());
    reload();
    mainLoop();

    syslog(LOG_INFO, "SIGTERM received, exiting");
    removePidFile();
    closelog();
    return 0;
}

// Если pid-файл указывает на живой процесс этой же программы - просим его завершиться (SIGTERM)
// и ждём, пока он исчезнет из /proc.
bool Daemon::stopPreviousInstance() {
    std::ifstream f(pidFile_);
    long pid = 0;
    if (!(f >> pid) || pid <= 0) return true;
    if (pid == static_cast<long>(getpid())) return true;
    if (!isSameProgram(static_cast<pid_t>(pid))) return true;  // файл устарел

    syslog(LOG_INFO, "found running instance pid=%ld, sending SIGTERM", pid);
    if (kill(static_cast<pid_t>(pid), SIGTERM) != 0 && errno != ESRCH) {
        reportError(std::string("cannot send SIGTERM to previous instance: ") + std::strerror(errno));
        return false;
    }
    for (int i = 0; i < kStopWaitSteps; ++i) {
        if (!isProcessAlive(static_cast<pid_t>(pid))) return true;
        usleep(100000);
    }
    reportError("previous instance did not stop in time");
    return false;
}

// Классическая демонизация: fork -> setsid -> fork -> chdir("/") -> закрыть дескрипторы -> /dev/null
bool Daemon::daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        reportError(std::string("fork failed: ") + std::strerror(errno));
        return false;
    }
    if (pid > 0) _exit(0);  // родитель завершается

    if (setsid() < 0) {  // новая сессия, управляющего терминала больше нет
        reportError(std::string("setsid failed: ") + std::strerror(errno));
        return false;
    }

    // Второй fork: демон перестаёт быть лидером сессии и не сможет случайно получить терминал
    pid = fork();
    if (pid < 0) {
        reportError(std::string("second fork failed: ") + std::strerror(errno));
        return false;
    }
    if (pid > 0) _exit(0);

    umask(022);
    if (chdir("/") != 0) {
        reportError(std::string("chdir failed: ") + std::strerror(errno));
        return false;
    }

    long maxFd = sysconf(_SC_OPEN_MAX);
    if (maxFd < 0 || maxFd > 4096) maxFd = 4096;
    for (int fd = 0; fd < maxFd; ++fd) close(fd);

    // Дескрипторы 0, 1, 2 (stdin, stdout, stderr) направляем в /dev/null
    int fd = open("/dev/null", O_RDWR);  // вернёт 0, т.к. все дескрипторы закрыты
    if (fd < 0) return false;
    if (dup2(fd, 1) < 0 || dup2(fd, 2) < 0) return false;
    if (fd > 2) close(fd);
    return true;
}

bool Daemon::writePidFile() const {
    std::ofstream f(pidFile_, std::ios::trunc);
    if (!f) return false;
    f << getpid() << "\n";
    return static_cast<bool>(f);
}

void Daemon::removePidFile() const {
    long pid = 0;
    std::ifstream f(pidFile_);
    // Удаляем файл только если он наш (новый экземпляр мог уже записать свой pid)
    if ((f >> pid) && pid == static_cast<long>(getpid())) unlink(pidFile_.c_str());
}

void Daemon::onSignal(int sig) {
    // В обработчике только выставляем флаги - всё остальное делает основной цикл
    if (sig == SIGHUP) gotHup_ = 1;
    else if (sig == SIGTERM) gotTerm_ = 1;
}

void Daemon::installSignalHandlers() const {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &Daemon::onSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    // Запущенные терминалы не должны становиться зомби
    struct sigaction ign;
    std::memset(&ign, 0, sizeof(ign));
    ign.sa_handler = SIG_IGN;
    sigemptyset(&ign.sa_mask);
    sigaction(SIGCHLD, &ign, nullptr);
}

// (Пере)читывает конфигурационный файл по запомненному абсолютному пути.
void Daemon::reload() {
    std::vector<Event> events;
    std::vector<std::string> errors;
    if (!loadEventsFromFile(configPath_, events, errors)) {
        syslog(LOG_ERR, "%s; keeping previous events", errors.empty() ? "cannot read config" : errors[0].c_str());
        return;
    }
    for (const std::string& e : errors) syslog(LOG_WARNING, "%s: %s", configPath_.c_str(), e.c_str());

    std::time_t now = std::time(nullptr);
    std::vector<Item> items;
    std::set<std::string> seen;
    for (const Event& ev : events) {
        if (!seen.insert(ev.key()).second) continue;  // дубликаты строк игнорируем
        auto it = handled_.find(ev.key());
        std::time_t threshold = (it != handled_.end()) ? it->second : now - kGraceSeconds;
        items.push_back({ev, firstOccurrenceAfter(ev, threshold)});
    }
    items_.swap(items);
    syslog(LOG_INFO, "configuration loaded: %zu event(s)", items_.size());
}

void Daemon::tick() {
    std::time_t now = std::time(nullptr);
    for (Item& item : items_) {
        if (item.next == NO_OCCURRENCE || item.next > now) continue;
        syslog(LOG_INFO, "reminder fired: %s", item.event.text.c_str());
        launchTerminal(item.event.text);
        handled_[item.event.key()] = now;
        item.next = firstOccurrenceAfter(item.event, now);
    }
}

void Daemon::mainLoop() {
    while (!gotTerm_) {
        if (gotHup_) {
            gotHup_ = 0;
            syslog(LOG_INFO, "SIGHUP received, reloading configuration");
            reload();
        }
        tick();
        struct timespec ts = {1, 0};
        nanosleep(&ts, nullptr);  // прерывается сигналом - реакция на SIGHUP/SIGTERM быстрая
    }
}

// Открывает новый терминал и показывает в нём текст напоминания.
void Daemon::launchTerminal(const std::string& text) {
    char tmpl[] = "/tmp/reminderd_msg_XXXXXX";
    int fd = mkstemp(tmpl);
    if (fd < 0) {
        syslog(LOG_ERR, "mkstemp failed: %s", std::strerror(errno));
        return;
    }
    bool written = writeAll(fd, buildMessageScript(text));
    close(fd);
    std::string scriptPath = tmpl;
    if (!written) {
        syslog(LOG_ERR, "cannot write message script");
        unlink(scriptPath.c_str());
        return;
    }

    std::vector<std::string> args;
    bool wsl = false;
    if (!buildTerminalCommand(scriptPath, args, wsl)) {
        syslog(LOG_ERR, "no terminal emulator available, reminder only logged");
        unlink(scriptPath.c_str());
        return;
    }

    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "fork failed: %s", std::strerror(errno));
        unlink(scriptPath.c_str());
        return;
    }
    if (pid == 0) {
        if (wsl) {
            // cmd.exe не любит UNC-путь вида \\wsl$\... в качестве рабочего каталога
            int r = chdir("/mnt/c");
            (void)r;
        }
        std::vector<char*> argv;
        for (std::string& a : args) argv.push_back(&a[0]);
        argv.push_back(nullptr);
        execv(argv[0], argv.data());
        _exit(127);
    }
}
