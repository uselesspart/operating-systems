#include "daemon.hpp"

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <sys/stat.h>
#include <syslog.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

volatile sig_atomic_t Daemon::reloadRequested_ = 0;
volatile sig_atomic_t Daemon::terminateRequested_ = 0;

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

void Daemon::handleSignal(int signalNumber) {
    if (signalNumber == SIGHUP) {
        reloadRequested_ = 1;
    } else if (signalNumber == SIGTERM) {
        terminateRequested_ = 1;
    }
}

void Daemon::start(const fs::path& configPath) {
    configPath_ = fs::absolute(configPath).lexically_normal();

    const char* pidOverride = std::getenv("OS_LAB1_PID_FILE");
    if (pidOverride != nullptr && *pidOverride != '\0') {
        pidFilePath_ = fs::absolute(pidOverride).lexically_normal();
    } else {
        pidFilePath_ = fs::path("/tmp") /
                       ("os_lab1_variant14_" +
                        std::to_string(static_cast<long long>(getuid())) + ".pid");
    }

    openlog("os_lab1_v14", LOG_PID | LOG_NDELAY, LOG_USER);

    if (!fs::exists(configPath_)) {
        syslog(LOG_ERR, "Configuration file does not exist: %s", configPath_.c_str());
        closelog();
        throw std::runtime_error("Configuration file does not exist: " + configPath_.string());
    }

    try {
        stopPreviousInstance();
        daemonize();

        syslog(LOG_INFO, "Daemon started. Config: %s", configPath_.c_str());

        installSignalHandlers();
        writePidFile();
        loadConfig();
        runLoop();

        syslog(LOG_INFO, "Daemon is stopping");
        removePidFile();
        closelog();
    } catch (const std::exception& error) {
        syslog(LOG_ERR, "Fatal error: %s", error.what());
        removePidFile();
        closelog();
        throw;
    }
}

void Daemon::stopPreviousInstance() {
    if (!fs::exists(pidFilePath_)) {
        return;
    }

    std::ifstream pidFile(pidFilePath_);
    pid_t oldPid = 0;
    pidFile >> oldPid;

    if (oldPid <= 0) {
        fs::remove(pidFilePath_);
        return;
    }

    const fs::path procPath = fs::path("/proc") / std::to_string(oldPid);
    if (!fs::exists(procPath)) {
        fs::remove(pidFilePath_);
        return;
    }

    if (kill(oldPid, SIGTERM) == -1 && errno != ESRCH) {
        throw std::runtime_error("Cannot stop previous daemon process: " +
                                 std::string(std::strerror(errno)));
    }

    for (int i = 0; i < 50 && fs::exists(procPath); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    if (fs::exists(procPath)) {
        throw std::runtime_error("Previous daemon did not terminate in time");
    }
}

void Daemon::daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        throw std::runtime_error("First fork failed: " + std::string(std::strerror(errno)));
    }
    if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        throw std::runtime_error("setsid failed: " + std::string(std::strerror(errno)));
    }

    pid = fork();
    if (pid < 0) {
        throw std::runtime_error("Second fork failed: " + std::string(std::strerror(errno)));
    }
    if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }

    umask(0);

    if (chdir("/") != 0) {
        throw std::runtime_error("chdir failed: " + std::string(std::strerror(errno)));
    }

    const int nullFd = open("/dev/null", O_RDWR);
    if (nullFd < 0) {
        throw std::runtime_error("Cannot open /dev/null: " + std::string(std::strerror(errno)));
    }

    if (dup2(nullFd, STDIN_FILENO) < 0 ||
        dup2(nullFd, STDOUT_FILENO) < 0 ||
        dup2(nullFd, STDERR_FILENO) < 0) {
        close(nullFd);
        throw std::runtime_error("dup2 failed: " + std::string(std::strerror(errno)));
    }

    if (nullFd > STDERR_FILENO) {
        close(nullFd);
    }
}

void Daemon::installSignalHandlers() {
    struct sigaction action {};
    action.sa_handler = Daemon::handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    if (sigaction(SIGHUP, &action, nullptr) == -1 ||
        sigaction(SIGTERM, &action, nullptr) == -1) {
        throw std::runtime_error("Cannot install signal handlers");
    }
}

void Daemon::writePidFile() {
    std::ofstream pidFile(pidFilePath_, std::ios::trunc);
    if (!pidFile) {
        throw std::runtime_error("Cannot open pid file: " + pidFilePath_.string());
    }
    pidFile << getpid() << '\n';
}

void Daemon::removePidFile() const {
    std::ifstream pidFile(pidFilePath_);
    pid_t storedPid = 0;
    pidFile >> storedPid;

    if (storedPid == getpid()) {
        std::error_code error;
        fs::remove(pidFilePath_, error);
    }
}

void Daemon::loadConfig() {
    std::ifstream config(configPath_);
    if (!config) {
        throw std::runtime_error("Cannot open config file: " + configPath_.string());
    }

    std::vector<FolderRule> newRules;
    int newIntervalSeconds = 20;
    std::string line;
    const fs::path configDirectory = configPath_.parent_path();
    std::size_t lineNumber = 0;

    while (std::getline(config, line)) {
        ++lineNumber;

        if (line.empty() || line[0] == '#') {
            continue;
        }

        std::istringstream input(line);
        std::string first;
        std::string second;
        std::string extra;

        if (!(input >> first >> second) || (input >> extra)) {
            syslog(LOG_WARNING, "Invalid config line %zu: %s", lineNumber, line.c_str());
            continue;
        }

        if (first == "interval") {
            try {
                std::size_t parsed = 0;
                const int value = std::stoi(second, &parsed);
                if (parsed != second.size() || value <= 0) {
                    throw std::invalid_argument("invalid interval");
                }
                newIntervalSeconds = value;
            } catch (const std::exception&) {
                syslog(LOG_WARNING, "Invalid interval at line %zu: %s", lineNumber, second.c_str());
            }
            continue;
        }

        fs::path folder = first;
        if (folder.is_relative()) {
            folder = configDirectory / folder;
        }
        folder = fs::absolute(folder).lexically_normal();

        newRules.push_back({folder, second});
    }

    rules_ = std::move(newRules);
    intervalSeconds_ = newIntervalSeconds;
    syslog(LOG_INFO, "Configuration loaded: %zu rule(s), interval=%d sec",
           rules_.size(), intervalSeconds_);
}

void Daemon::processFolders() const {
    for (const FolderRule& rule : rules_) {
        std::error_code error;

        if (!fs::exists(rule.folder, error) || !fs::is_directory(rule.folder, error)) {
            syslog(LOG_WARNING, "Folder is unavailable: %s", rule.folder.c_str());
            continue;
        }

        const fs::path marker = rule.folder / rule.ignoreFile;
        if (fs::exists(marker, error)) {
            syslog(LOG_INFO, "Skip folder %s: marker %s exists",
                   rule.folder.c_str(), rule.ignoreFile.c_str());
            continue;
        }

        for (const auto& entry : fs::directory_iterator(rule.folder, error)) {
            if (error) {
                syslog(LOG_ERR, "Cannot read folder %s: %s",
                       rule.folder.c_str(), error.message().c_str());
                break;
            }

            std::error_code removeError;
            fs::remove_all(entry.path(), removeError);
            if (removeError) {
                syslog(LOG_ERR, "Cannot remove %s: %s",
                       entry.path().c_str(), removeError.message().c_str());
            }
        }

        syslog(LOG_INFO, "Folder processed: %s", rule.folder.c_str());
    }
}

void Daemon::runLoop() {
    int elapsedSeconds = intervalSeconds_;

    while (!terminateRequested_) {
        if (reloadRequested_) {
            reloadRequested_ = 0;
            try {
                loadConfig();
                syslog(LOG_INFO, "Configuration reloaded after SIGHUP");
            } catch (const std::exception& error) {
                syslog(LOG_ERR, "Config reload failed: %s", error.what());
            }
        }

        if (elapsedSeconds >= intervalSeconds_) {
            processFolders();
            elapsedSeconds = 0;
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
        ++elapsedSeconds;
    }
}
