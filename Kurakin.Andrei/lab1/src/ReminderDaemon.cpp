#include "ReminderDaemon.h"
#include "Scheduler.h"

#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <syslog.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

volatile sig_atomic_t stopRequested = 0;
volatile sig_atomic_t reloadRequested = 0;

void signalHandler(int signalNumber) {
    if (signalNumber == SIGTERM) {
        stopRequested = 1;
    } else if (signalNumber == SIGHUP) {
        reloadRequested = 1;
    }
}

std::string linuxExecutable(pid_t pid) {
    const std::string link = "/proc/" + std::to_string(pid) + "/exe";
    char buffer[PATH_MAX + 1];
    const ssize_t length = ::readlink(link.c_str(), buffer, sizeof(buffer) - 1);
    if (length < 0) {
        return "";
    }
    buffer[length] = '\0';
    return buffer;
}

std::string executablePathWithoutDeletedSuffix(std::string path) {
    const std::string suffix = " (deleted)";
    if (path.size() > suffix.size() &&
        path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
        path.erase(path.size() - suffix.size());
    }
    return path;
}

pid_t readPid(int fd) {
    if (::lseek(fd, 0, SEEK_SET) < 0) {
        return 0;
    }
    char data[64] = {};
    const ssize_t length = ::read(fd, data, sizeof(data) - 1);
    if (length <= 0) {
        return 0;
    }
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(data, &end, 10);
    if (errno != 0 || end == data || value <= 0 || value > INT_MAX) {
        return 0;
    }
    return static_cast<pid_t>(value);
}

void writeAll(int fd, const std::string& text) {
    std::size_t offset = 0;
    while (offset < text.size()) {
        const ssize_t written = ::write(fd, text.data() + offset, text.size() - offset);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            throw std::runtime_error("write failed: " + std::string(std::strerror(errno)));
        }
        offset += static_cast<std::size_t>(written);
    }
}

bool onPath(const std::string& executable) {
    const char* envPath = std::getenv("PATH");
    const std::string path = envPath == nullptr ? "/usr/bin:/bin" : envPath;
    std::size_t begin = 0;
    while (begin <= path.size()) {
        const std::size_t end = path.find(':', begin);
        const std::string directory = path.substr(begin, end - begin);
        const std::string full = (directory.empty() ? "." : directory) + "/" + executable;
        if (::access(full.c_str(), X_OK) == 0) {
            struct stat info = {};
            if (::stat(full.c_str(), &info) == 0 && S_ISREG(info.st_mode)) {
                return true;
            }
        }
        if (end == std::string::npos) {
            break;
        }
        begin = end + 1;
    }
    return false;
}

void oneSecondSleep() {
    timespec requested = {1, 0};
    while (::nanosleep(&requested, &requested) < 0 && errno == EINTR) {
        if (stopRequested || reloadRequested) {
            return;
        }
    }
}

void reportStartup(int fd, char result) {
    while (::write(fd, &result, 1) < 0 && errno == EINTR) {
    }
    ::close(fd);
}

} // конец анонимного пространства имён

ReminderDaemon& ReminderDaemon::instance() {
    static ReminderDaemon daemon;
    return daemon;
}

ReminderDaemon::~ReminderDaemon() {
    if (pidFd_ >= 0) {
        ::close(pidFd_);
    }
}

void ReminderDaemon::acquirePidFile() {
    pidFd_ = ::open(pidPath_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (pidFd_ < 0) {
        throw std::runtime_error("Cannot open PID file: " +
                                 std::string(std::strerror(errno)));
    }

    const std::string myExecutable = linuxExecutable(::getpid());
    pid_t signalled = 0;
    // Если уже есть демон, сначала спокойно останавливаем его.
    // Сигналим только нашему бинарнику, чтобы не задеть чужой процесс с тем же PID.
    for (int attempt = 0; attempt < 60; ++attempt) {
        const pid_t existing = readPid(pidFd_);
        if (existing > 0 && existing != ::getpid() && existing != signalled &&
            !myExecutable.empty() && executablePathWithoutDeletedSuffix(linuxExecutable(existing)) ==
                myExecutable) {
            if (::kill(existing, SIGTERM) == 0) {
                signalled = existing;
            } else if (errno != ESRCH) {
                throw std::runtime_error("Cannot terminate previous daemon: " +
                                         std::string(std::strerror(errno)));
            }
        }

        if (::flock(pidFd_, LOCK_EX | LOCK_NB) == 0) {
            return;
        }
        if (errno != EWOULDBLOCK && errno != EAGAIN) {
            throw std::runtime_error("Cannot lock PID file: " +
                                     std::string(std::strerror(errno)));
        }
        timespec pause = {0, 100000000}; // 100 мс, всего ждём до 6 секунд.
        ::nanosleep(&pause, nullptr);
    }
    throw std::runtime_error("Previous daemon did not release the PID file lock");
}

void ReminderDaemon::writePid() const {
    if (::ftruncate(pidFd_, 0) != 0 || ::lseek(pidFd_, 0, SEEK_SET) < 0) {
        throw std::runtime_error("Cannot reset PID file");
    }
    writeAll(pidFd_, std::to_string(::getpid()) + "\n");
    if (::fsync(pidFd_) != 0) {
        throw std::runtime_error("Cannot sync PID file");
    }
}

void ReminderDaemon::clearPid() {
    // Оставляем заблокированный inode на месте. Если удалить его во время ожидания,
    // новый демон может получить другой PID-файл и запуститься параллельно.
    if (pidFd_ >= 0) {
        if (readPid(pidFd_) == ::getpid()) {
            (void)::ftruncate(pidFd_, 0);
            (void)::fsync(pidFd_);
        }
        ::close(pidFd_);
        pidFd_ = -1;
    }
}

void ReminderDaemon::installSignalHandlers() const {
    struct sigaction action = {};
    action.sa_handler = signalHandler;
    ::sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (::sigaction(SIGTERM, &action, nullptr) != 0 ||
        ::sigaction(SIGHUP, &action, nullptr) != 0) {
        throw std::runtime_error("Cannot install signal handlers");
    }
}

int ReminderDaemon::start(const std::filesystem::path& configFile) {
    // Пока доступна исходная папка, сохраняем абсолютные пути до chdir("/").
    configPath_ = std::filesystem::absolute(configFile).lexically_normal();
    pidPath_ = configPath_.parent_path() / "reminder.pid";
    config_ = loadConfiguration(configPath_);
    acquirePidFile();

    int startupPipe[2];
    if (::pipe(startupPipe) != 0) {
        throw std::runtime_error("Cannot create startup pipe");
    }
    const pid_t firstChild = ::fork();
    if (firstChild < 0) {
        ::close(startupPipe[0]);
        ::close(startupPipe[1]);
        throw std::runtime_error("First fork failed");
    }
    if (firstChild > 0) {
        ::close(startupPipe[1]);
        ::close(pidFd_);
        pidFd_ = -1;
        char result = '0';
        ssize_t bytes = -1;
        do {
            bytes = ::read(startupPipe[0], &result, 1);
        } while (bytes < 0 && errno == EINTR);
        ::close(startupPipe[0]);
        (void)::waitpid(firstChild, nullptr, 0);
        if (bytes == 1 && result == '1') {
            std::cout << "Daemon started. PID file: " << pidPath_ << '\n';
            return 0;
        }
        std::cerr << "Daemon failed to initialize; inspect the system journal.\n";
        return 1;
    }

    ::close(startupPipe[0]);
    if (::setsid() == -1) {
        reportStartup(startupPipe[1], '0');
        ::_exit(1);
    }
    const pid_t secondChild = ::fork();
    if (secondChild < 0) {
        reportStartup(startupPipe[1], '0');
        ::_exit(1);
    }
    if (secondChild > 0) {
        ::_exit(0);
    }

    ::umask(022);
    if (::chdir("/") != 0) {
        reportStartup(startupPipe[1], '0');
        ::_exit(1);
    }
    const int nullFd = ::open("/dev/null", O_RDWR);
    if (nullFd < 0 || ::dup2(nullFd, STDIN_FILENO) == -1 ||
        ::dup2(nullFd, STDOUT_FILENO) == -1 ||
        ::dup2(nullFd, STDERR_FILENO) == -1) {
        reportStartup(startupPipe[1], '0');
        ::_exit(1);
    }
    if (nullFd > STDERR_FILENO) {
        ::close(nullFd);
    }

    ::openlog("reminder_daemon", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    try {
        std::filesystem::create_directories(config_.stateDirectory);
        if (!std::filesystem::is_directory(config_.stateDirectory)) {
            throw std::runtime_error("state_dir is not a directory");
        }
        installSignalHandlers();
        writePid();
    } catch (const std::exception& error) {
        ::syslog(LOG_ERR, "Startup error: %s", error.what());
        reportStartup(startupPipe[1], '0');
        clearPid();
        ::closelog();
        ::_exit(1);
    }
    ::syslog(LOG_INFO, "Daemon started: config=%s, interval=%d, events=%zu",
             configPath_.c_str(), config_.intervalSeconds, config_.events.size());
    reportStartup(startupPipe[1], '1');

    runLoop();
    ::syslog(LOG_INFO, "SIGTERM received. Daemon shutting down.");
    clearPid();
    ::closelog();
    return 0;
}

void ReminderDaemon::reloadConfiguration() {
    try {
        Configuration updated = loadConfiguration(configPath_);
        std::filesystem::create_directories(updated.stateDirectory);
        if (!std::filesystem::is_directory(updated.stateDirectory)) {
            throw std::runtime_error("state_dir is not a directory");
        }
        config_ = std::move(updated);
        ::syslog(LOG_INFO, "SIGHUP: config reloaded; interval=%d, events=%zu",
                 config_.intervalSeconds, config_.events.size());
    } catch (const std::exception& error) {
        // Если новый конфиг плохой, продолжаем работать со старым.
        ::syslog(LOG_ERR, "SIGHUP: reload rejected: %s", error.what());
    }
}

bool ReminderDaemon::alreadyFired(const ReminderEvent& event, std::time_t due) const {
    const auto path = config_.stateDirectory / (event.id + ".last");
    std::ifstream file(path);
    long long last = -1;
    return (file >> last) && last == static_cast<long long>(due);
}

bool ReminderDaemon::rememberFiring(const ReminderEvent& event, std::time_t due) const {
    const auto path = config_.stateDirectory / (event.id + ".last");
    const auto temporary = config_.stateDirectory /
                           (event.id + "." + std::to_string(::getpid()) + ".tmp");
    const int fd = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC,
                          0600);
    if (fd == -1) {
        ::syslog(LOG_ERR, "Cannot create state file: %s", std::strerror(errno));
        return false;
    }
    try {
        writeAll(fd, std::to_string(static_cast<long long>(due)) + "\n");
        if (::fsync(fd) != 0) {
            throw std::runtime_error(std::strerror(errno));
        }
        if (::close(fd) != 0) {
            throw std::runtime_error("Cannot close state file");
        }
        std::filesystem::rename(temporary, path);
        return true;
    } catch (const std::exception& error) {
        ::syslog(LOG_ERR, "Cannot persist reminder state: %s", error.what());
        ::unlink(temporary.c_str());
        return false;
    }
}

bool ReminderDaemon::openTerminal(const std::string& text) const {
    if (std::getenv("DISPLAY") == nullptr &&
        std::getenv("WAYLAND_DISPLAY") == nullptr) {
        ::syslog(LOG_ERR, "Cannot open a terminal without a graphical user session");
        return false;
    }

    std::string terminal = config_.terminal;
    if (terminal == "auto") {
        for (const char* candidate : {"gnome-terminal", "konsole", "xterm",
                                       "x-terminal-emulator"}) {
            if (onPath(candidate)) {
                terminal = candidate;
                break;
            }
        }
    }
    if (terminal == "auto" || !onPath(terminal)) {
        ::syslog(LOG_ERR, "No supported terminal emulator found (install xterm)");
        return false;
    }

    const pid_t child = ::fork();
    if (child < 0) {
        ::syslog(LOG_ERR, "Cannot fork terminal: %s", std::strerror(errno));
        return false;
    }
    if (child == 0) {
        (void)::setsid();
        // Текст приходит как $1 и не подставляется прямо в shell-код.
        const std::string script =
            "printf '\\n=== REMINDER ===\\n%s\\n\\nPress Enter to close...\\n' \"$1\"; "
            "IFS= read -r unused";
        std::vector<std::string> arguments;
        arguments.push_back(terminal);
        if (terminal == "gnome-terminal") {
            arguments.push_back("--");
        } else if (terminal == "xterm") {
            arguments.push_back("-e");
        } else {
            arguments.push_back("-e");
        }
        arguments.insert(arguments.end(), {"/bin/sh", "-c", script, "reminder", text});
        std::vector<char*> argv;
        for (std::string& argument : arguments) {
            argv.push_back(argument.data());
        }
        argv.push_back(nullptr);
        ::execvp(terminal.c_str(), argv.data());
        ::syslog(LOG_ERR, "Failed to exec %s: %s", terminal.c_str(),
                 std::strerror(errno));
        ::_exit(127);
    }
    return true;
}

void ReminderDaemon::processReminders(std::time_t now) {
    for (const ReminderEvent& event : config_.events) {
        const auto due = currentOccurrence(event, now);
        if (!due.has_value()) {
            continue;
        }
        const std::time_t window = std::max(60, config_.intervalSeconds + 5);
        if (now < *due || now - *due >= window || alreadyFired(event, *due)) {
            continue;
        }
        if (openTerminal(event.text)) {
            if (rememberFiring(event, *due)) {
                ::syslog(LOG_INFO, "Reminder shown: %s", event.text.c_str());
            }
        }
    }
}

void ReminderDaemon::reapTerminalProcesses() const {
    while (::waitpid(-1, nullptr, WNOHANG) > 0) {
    }
}

void ReminderDaemon::runLoop() {
    while (!stopRequested) {
        if (reloadRequested) {
            reloadRequested = 0;
            reloadConfiguration();
        }
        processReminders(std::time(nullptr));
        reapTerminalProcesses();
        for (int second = 0; second < config_.intervalSeconds &&
                             !stopRequested && !reloadRequested; ++second) {
            oneSecondSleep();
        }
    }
    reapTerminalProcesses();
}
