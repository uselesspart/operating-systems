#include "daemon.h"

#include <cerrno>
#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <syslog.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {
volatile sig_atomic_t stopRequested = 0;
volatile sig_atomic_t reloadRequested = 0;
sigset_t waitMask;

void handleSignal(int signal) {
    if (signal == SIGHUP) reloadRequested = 1;
    else if (signal == SIGTERM || signal == SIGINT) stopRequested = 1;
}

void fail(const std::string& operation) {
    throw std::runtime_error(operation + ": " + strerror(errno));
}

int openLock(const std::string& path) {
    const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd == -1) fail("open " + path);
    struct stat info{};
    if (fstat(fd, &info) == -1 || !S_ISREG(info.st_mode) || info.st_uid != getuid() || info.st_nlink != 1) {
        close(fd);
        throw std::runtime_error("Небезопасный файл блокировки: " + path);
    }
    return fd;
}

std::string startTicks(pid_t pid) {
    std::ifstream file("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (!std::getline(file, line)) throw std::runtime_error("Не прочитать /proc/PID/stat");
    const auto bracket = line.rfind(')');
    if (bracket == std::string::npos) throw std::runtime_error("Некорректный /proc/PID/stat");
    std::istringstream fields(line.substr(bracket + 2));
    std::string field;
    for (int i = 3; i <= 22; ++i) {
        if (!(fields >> field)) throw std::runtime_error("Не найдено время запуска процесса");
    }
    return field;
}

void writeAll(int fd, const std::string& value) {
    std::size_t offset = 0;
    while (offset < value.size()) {
        const auto count = write(fd, value.data() + offset, value.size() - offset);
        if (count == -1 && errno == EINTR) continue;
        if (count <= 0) fail("write");
        offset += static_cast<std::size_t>(count);
    }
}

void installSignals() {
    struct sigaction action{};
    action.sa_handler = handleSignal;
    sigemptyset(&action.sa_mask);
    for (int signal : {SIGHUP, SIGTERM, SIGINT, SIGCHLD}) {
        if (sigaction(signal, &action, nullptr) == -1) fail("sigaction");
    }
    sigset_t blocked;
    sigemptyset(&blocked);
    for (int signal : {SIGHUP, SIGTERM, SIGINT, SIGCHLD}) sigaddset(&blocked, signal);
    if (sigprocmask(SIG_BLOCK, &blocked, &waitMask) == -1) fail("sigprocmask");
    for (int signal : {SIGHUP, SIGTERM, SIGINT, SIGCHLD}) sigdelset(&waitMask, signal);
}

bool ownsPidDescriptor(const fs::path& proc, int fd) {
    struct stat expected{};
    if (fstat(fd, &expected) == -1) fail("fstat PID file");
    for (const auto& entry : fs::directory_iterator(proc / "fd")) {
        struct stat actual{};
        if (stat(entry.path().c_str(), &actual) == 0 && actual.st_dev == expected.st_dev && actual.st_ino == expected.st_ino)
            return true;
    }
    return false;
}

void closeOtherDescriptors(const std::vector<int>& keep) {
    std::vector<int> descriptors;
    for (const auto& entry : fs::directory_iterator("/proc/self/fd")) {
        const int fd = std::stoi(entry.path().filename().string());
        if (fd > 2 && std::find(keep.begin(), keep.end(), fd) == keep.end()) descriptors.push_back(fd);
    }
    for (int fd : descriptors) {
        if (close(fd) == -1 && errno != EBADF && errno != EINTR) fail("close inherited descriptor");
    }
}
}

std::string findTerminal(const std::string& requested) {
    const std::vector<std::string> supported = {"ptyxis", "gnome-terminal", "kgx", "konsole", "xfce4-terminal", "xterm"};
    auto resolve = [](const std::string& name) -> std::string {
        if (name.find('/') != std::string::npos)
            return access(name.c_str(), X_OK) == 0 ? fs::absolute(name).string() : "";
        std::istringstream paths(std::getenv("PATH") ? std::getenv("PATH") : "/usr/bin:/bin");
        std::string folder;
        while (std::getline(paths, folder, ':')) {
            const auto candidate = fs::absolute(fs::path(folder.empty() ? "." : folder) / name);
            if (access(candidate.c_str(), X_OK) == 0) return candidate.string();
        }
        return "";
    };
    for (const auto& name : supported) {
        if (requested != "auto" && fs::path(requested).filename() != name) continue;
        const auto path = resolve(requested == "auto" ? name : requested);
        if (!path.empty()) return path;
    }
    throw std::runtime_error("Не найден поддерживаемый терминал: ptyxis, gnome-terminal, kgx, konsole, xfce4-terminal, xterm");
}

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

Daemon::~Daemon() {
    for (const auto& notification : pending) unlink(notification.acknowledgement.c_str());
    if (pidFd != -1) {
        if (ownsPid && ftruncate(pidFd, 0) == -1) syslog(LOG_ERR, "Cannot clear PID file: %s", strerror(errno));
        close(pidFd);
    }
    closelog();
}

void Daemon::takePidFile() {
    pidFd = openLock(pidPath);
    if (flock(pidFd, LOCK_EX | LOCK_NB) == 0) return;
    if (errno != EWOULDBLOCK) fail("flock PID file");
    std::ifstream file(pidPath);
    long oldPid = 0;
    std::string ticks, oldConfig;
    if (!(file >> oldPid >> ticks >> std::quoted(oldConfig)) || oldPid <= 1 || oldPid > 4194304 || oldConfig != configPath)
        throw std::runtime_error("Занятый PID-файл содержит некорректные данные");
    const pid_t pid = static_cast<pid_t>(oldPid);
    const int handle = static_cast<int>(syscall(SYS_pidfd_open, pid, 0));
    if (handle == -1) fail("pidfd_open previous daemon");
    try {
        const fs::path proc = "/proc/" + std::to_string(pid);
        struct stat info{};
        std::string oldExecutable = fs::read_symlink(proc / "exe").string();
        const std::string suffix = " (deleted)";
        if (oldExecutable.size() >= suffix.size() && oldExecutable.compare(oldExecutable.size() - suffix.size(), suffix.size(), suffix) == 0)
            oldExecutable.resize(oldExecutable.size() - suffix.size());
        if (stat(proc.c_str(), &info) == -1 || info.st_uid != getuid() ||
            oldExecutable != executable || startTicks(pid) != ticks || !ownsPidDescriptor(proc, pidFd))
            throw std::runtime_error("PID принадлежит другому процессу; сигнал не отправлен");
        if (syscall(SYS_pidfd_send_signal, handle, SIGTERM, nullptr, 0) == -1 && errno != ESRCH)
            fail("pidfd_send_signal SIGTERM");
    } catch (...) {
        close(handle);
        throw;
    }
    close(handle);
    for (int attempt = 0; attempt < 100; ++attempt) {
        if (flock(pidFd, LOCK_EX | LOCK_NB) == 0) return;
        if (errno != EWOULDBLOCK) fail("flock PID file");
        timespec delay{0, 50000000};
        while (nanosleep(&delay, &delay) == -1) {
            if (errno != EINTR) fail("nanosleep");
        }
    }
    throw std::runtime_error("Предыдущий демон не завершился за 5 секунд");
}

void Daemon::writePid() {
    std::ostringstream record;
    record << getpid() << '\n' << startTicks(getpid()) << '\n' << std::quoted(configPath) << '\n';
    if (ftruncate(pidFd, 0) == -1 || lseek(pidFd, 0, SEEK_SET) == -1) fail("prepare PID file");
    writeAll(pidFd, record.str());
    ownsPid = true;
    if (fsync(pidFd) == -1) fail("fsync PID file");
}

void Daemon::readState() {
    const std::string path = configPath + ".state";
    if (!fs::exists(path)) return;
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Не прочитать состояние событий");
    std::string key;
    std::time_t time;
    while (input >> std::ws && input.peek() != EOF) {
        if (!(input >> std::quoted(key) >> time) || time < 0)
            throw std::runtime_error("Повреждён файл состояния: " + path);
        delivered[key] = time;
    }
    if (input.bad()) throw std::runtime_error("Ошибка чтения состояния");
}

void Daemon::saveState() {
    const std::string destination = configPath + ".state";
    std::string temporary = destination + ".tmp.XXXXXX";
    const int fd = mkstemp(temporary.data());
    if (fd == -1) fail("mkstemp state");
    try {
        std::ostringstream data;
        for (const auto& [key, time] : delivered) data << std::quoted(key) << ' ' << time << '\n';
        writeAll(fd, data.str());
        if (fsync(fd) == -1) fail("fsync state");
        if (close(fd) == -1) fail("close state");
    } catch (...) {
        close(fd);
        unlink(temporary.c_str());
        throw;
    }
    if (rename(temporary.c_str(), destination.c_str()) == -1) {
        const int saved = errno;
        unlink(temporary.c_str());
        errno = saved;
        fail("rename state");
    }
}

void Daemon::reload(bool initial) {
    try {
        Config candidate = readConfig(configPath);
        const std::string candidateTerminal = findTerminal(candidate.terminal);
        config = std::move(candidate);
        terminal = candidateTerminal;
        if (reloadRequested) syslog(LOG_INFO, "Configuration reloaded: %zu events", config.events.size());
    } catch (const std::exception& error) {
        if (initial) throw;
        syslog(LOG_ERR, "Configuration unchanged: %s", error.what());
    }
}

bool Daemon::notify(const Event& event, std::time_t due) {
    std::string acknowledgement = configPath + ".notify.XXXXXX";
    const int marker = mkstemp(acknowledgement.data());
    if (marker == -1) fail("mkstemp notification");
    if (close(marker) == -1) { unlink(acknowledgement.c_str()); fail("close notification"); }
    const std::string name = fs::path(terminal).filename();
    std::vector<std::string> args{terminal};
    if (name == "ptyxis") args.push_back("--new-window");
    if (name == "ptyxis" || name == "gnome-terminal" || name == "kgx") args.push_back("--");
    else if (name == "xfce4-terminal") args.push_back("--execute");
    else args.push_back("-e");
    args.insert(args.end(), {executable, "--show", event.text, "--ack-file", acknowledgement});
    std::vector<char*> argv;
    for (auto& arg : args) argv.push_back(arg.data());
    argv.push_back(nullptr);
    int errors[2];
    if (pipe2(errors, O_CLOEXEC) == -1) { unlink(acknowledgement.c_str()); fail("pipe2 terminal"); }
    const pid_t child = fork();
    if (child == -1) {
        close(errors[0]);
        close(errors[1]);
        unlink(acknowledgement.c_str());
        fail("fork terminal");
    }
    if (child == 0) {
        close(errors[0]);
        sigprocmask(SIG_SETMASK, &waitMask, nullptr);
        execv(terminal.c_str(), argv.data());
        const int saved = errno;
        const auto ignored = write(errors[1], &saved, sizeof(saved));
        (void)ignored;
        _exit(127);
    }
    close(errors[1]);
    int error = 0;
    ssize_t count;
    do { count = read(errors[0], &error, sizeof(error)); } while (count == -1 && errno == EINTR);
    close(errors[0]);
    if (count == -1) { unlink(acknowledgement.c_str()); fail("read terminal exec status"); }
    if (count != 0) {
        unlink(acknowledgement.c_str());
        syslog(LOG_ERR, "exec terminal: %s", strerror(error));
        return false;
    }
    pending.push_back({child, event.key, due, acknowledgement, std::chrono::steady_clock::now()});
    syslog(LOG_INFO, "Terminal started for reminder: %s", event.text.c_str());
    return true;
}

void Daemon::reapChildren() {
    int status = 0;
    pid_t child;
    while ((child = waitpid(-1, &status, WNOHANG)) > 0) {
        if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
            syslog(LOG_ERR, "Terminal child %ld failed, status=%d; reminder will be retried", static_cast<long>(child), status);
            for (auto it = pending.begin(); it != pending.end();) {
                if (it->child == child) { unlink(it->acknowledgement.c_str()); it = pending.erase(it); }
                else ++it;
            }
        }
    }
    if (child == -1 && errno != ECHILD && errno != EINTR) fail("waitpid");
}

void Daemon::checkNotifications() {
    for (auto it = pending.begin(); it != pending.end();) {
        std::ifstream input(it->acknowledgement);
        std::string result;
        std::getline(input, result);
        if (result == "ready") {
            delivered[it->key] = it->due;
            saveState();
            syslog(LOG_INFO, "Reminder displayed: %s", it->key.c_str());
            unlink(it->acknowledgement.c_str());
            it = pending.erase(it);
        } else if (std::chrono::steady_clock::now() - it->started > std::chrono::seconds(30)) {
            syslog(LOG_ERR, "No display acknowledgement; reminder will be retried: %s", it->key.c_str());
            unlink(it->acknowledgement.c_str());
            it = pending.erase(it);
        } else ++it;
    }
}

void Daemon::loop() {
    auto nextCheck = std::chrono::steady_clock::now();
    while (!stopRequested) {
        checkNotifications();
        reapChildren();
        if (reloadRequested || std::chrono::steady_clock::now() >= nextCheck) {
            reload(false);
            reloadRequested = 0;
            const auto now = std::time(nullptr);
            if (now == -1) fail("time");
            for (const auto& event : config.events) {
                const auto due = latestOccurrence(event, now);
                const auto found = delivered.find(event.key);
                const bool inProgress = std::any_of(pending.begin(), pending.end(), [&](const Notification& item) { return item.key == event.key; });
                if (!inProgress && due <= now && (found == delivered.end() || found->second < due)) notify(event, due);
            }
            nextCheck = std::chrono::steady_clock::now() + std::chrono::seconds(config.interval);
        }
        auto remaining = std::chrono::duration_cast<std::chrono::nanoseconds>(nextCheck - std::chrono::steady_clock::now());
        if (!pending.empty()) remaining = std::min(remaining, std::chrono::nanoseconds(100000000));
        if (remaining.count() < 0) remaining = std::chrono::nanoseconds(0);
        timespec delay{static_cast<time_t>(remaining.count() / 1000000000), static_cast<long>(remaining.count() % 1000000000)};
        const int result = pselect(0, nullptr, nullptr, nullptr, &delay, &waitMask);
        if (result == -1 && errno != EINTR) fail("pselect");
    }
    reapChildren();
    syslog(LOG_INFO, "Daemon stopped by SIGTERM/SIGINT");
}

int Daemon::run(const std::string& path, const std::string& pidFile, bool inForeground) {
    foreground = inForeground;
    openlog("reminder", LOG_PID | (foreground ? LOG_PERROR : 0), LOG_USER);
    configPath = fs::canonical(path).string();
    pidPath = fs::weakly_canonical(fs::absolute(pidFile)).string();
    if (pidPath == configPath || pidPath == configPath + ".state" || pidPath == configPath + ".lock")
        throw std::runtime_error("PID-файл должен отличаться от конфигурации и её служебных файлов");
    executable = fs::read_symlink("/proc/self/exe").string();
    reload(true);
    if (!std::getenv("DISPLAY") && !std::getenv("WAYLAND_DISPLAY"))
        throw std::runtime_error("Нет графической сессии: запускайте из терминала рабочего стола Ubuntu");
    const int startup = openLock(pidPath + ".start.lock");
    if (flock(startup, LOCK_EX | LOCK_NB) == -1) {
        close(startup);
        throw std::runtime_error("Другой запуск ещё не закончен; повторите команду");
    }
    try { takePidFile(); readState(); installSignals(); }
    catch (...) { close(startup); throw; }

    int ready = -1;
    if (!foreground) {
        int channel[2];
        if (pipe2(channel, O_CLOEXEC) == -1) { close(startup); fail("pipe2 startup"); }
        const pid_t child = fork();
        if (child == -1) { close(startup); close(channel[0]); close(channel[1]); fail("fork daemon"); }
        if (child > 0) {
            close(channel[1]);
            close(startup);
            close(pidFd);
            pidFd = -1;
            pollfd item{channel[0], POLLIN, 0};
            int result;
            do { result = poll(&item, 1, 15000); } while (result == -1 && errno == EINTR);
            char status = 'E';
            const auto count = result > 0 ? read(channel[0], &status, 1) : -1;
            close(channel[0]);
            int childStatus;
            while (waitpid(child, &childStatus, 0) == -1) {
                if (errno != EINTR) fail("waitpid daemon");
            }
            if (count != 1 || status != 'R') throw std::runtime_error("Демон не запустился; подробности: journalctl -t reminder");
            return 0;
        }
        close(channel[0]);
        ready = channel[1];
    }
    try {
        if (!foreground) {
            if (setsid() == -1) fail("setsid");
            const pid_t child = fork();
            if (child == -1) fail("second fork");
            if (child > 0) _exit(0);
            umask(0077);
            if (chdir("/") == -1) fail("chdir /");
            const int nullFd = open("/dev/null", O_RDWR | O_CLOEXEC);
            if (nullFd == -1) fail("open /dev/null");
            for (int target : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
                if (dup2(nullFd, target) == -1 || fcntl(target, F_SETFD, 0) == -1) fail("redirect /dev/null");
            }
            if (nullFd > STDERR_FILENO) close(nullFd);
            closelog();
            closeOtherDescriptors({pidFd, startup, ready});
            openlog("reminder", LOG_PID, LOG_USER);
        }
        writePid();
        syslog(LOG_INFO, "Daemon started: config=%s, interval=%d", configPath.c_str(), config.interval);
        if (ready != -1) { writeAll(ready, "R"); close(ready); ready = -1; }
        close(startup);
        loop();
        return 0;
    } catch (const std::exception& error) {
        syslog(LOG_ERR, "%s", error.what());
        if (ready != -1) { const auto ignored = write(ready, "E", 1); (void)ignored; close(ready); close(startup); }
        return 1;
    }
}
