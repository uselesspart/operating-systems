#include "daemon.hpp"
#include "config/config.hpp"
#include "worker/worker.hpp"

#include <iostream>
#include <filesystem>
#include <fstream>
#include <system_error>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <cstring>

namespace {
volatile sig_atomic_t g_running = 1;
volatile sig_atomic_t g_reload = 0;

void signalHandler(int signum) {
    if (signum == SIGTERM) {
        g_running = 0;
    } else if (signum == SIGHUP) {
        g_reload = 1;
    }
}
} // namespace

namespace monitor {

Daemon& Daemon::getInstance() {
    static Daemon instance;
    return instance;
}

void Daemon::init(const std::string& configPath) {
    std::error_code ec;
    std::filesystem::path p = std::filesystem::canonical(configPath, ec);
    
    if (!ec) {
        configPath_ = p.string();
    } else {
        configPath_ = std::filesystem::absolute(configPath).string(); 
    }

    openlog("disk_monitor", LOG_PID, LOG_LOCAL0);

    checkAndKillExisting();
    daemonize();
    setupSignals();
    writePidFile();
}

void Daemon::checkAndKillExisting() {
    std::ifstream pidFile(pidFilePath_);
    if (!pidFile.is_open()) {
        return;
    }

    pid_t oldPid;
    if (pidFile >> oldPid) {
        std::string procPath = "/proc/" + std::to_string(oldPid);
        struct stat sts;
        
        if (stat(procPath.c_str(), &sts) == 0 && S_ISDIR(sts.st_mode)) {
            syslog(LOG_INFO, "A running process has been detected. (PID %d). Sending SIGTERM...", oldPid);
            
            if (kill(oldPid, SIGTERM) == 0) {
                int retries = 50;
                while (stat(procPath.c_str(), &sts) == 0 && retries-- > 0) {
                    usleep(100000);
                }
            } else {
                syslog(LOG_ERR, "Failed to send SIGTERM to process %d", oldPid);
            }
        }
    }
}

void Daemon::daemonize() {
    pid_t pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Error in the first fork(): %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        syslog(LOG_ERR, "Error in setsid(): %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    signal(SIGHUP, SIG_IGN);

    pid = fork();
    if (pid < 0) {
        syslog(LOG_ERR, "Error in the second fork(): %s", strerror(errno));
        exit(EXIT_FAILURE);
    }
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }

    umask(0);

    if (chdir("/") < 0) {
        syslog(LOG_ERR, "Error in chdir(\"/\"): %s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    int fd = open("/dev/null", O_RDWR);
    if (fd != -1) {
        dup2(fd, STDIN_FILENO);
        dup2(fd, STDOUT_FILENO);
        dup2(fd, STDERR_FILENO);
        if (fd > STDERR_FILENO) {
            close(fd);
        }
    }

    syslog(LOG_INFO, "Daemonization completed successfully. PID: %d", getpid());
}

void Daemon::setupSignals() {
    struct sigaction sa;
    sa.sa_handler = signalHandler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    if (sigaction(SIGTERM, &sa, nullptr) < 0) {
        syslog(LOG_ERR, "Error setting up SIGTERM handler: %s", strerror(errno));
    }
    if (sigaction(SIGHUP, &sa, nullptr) < 0) {
        syslog(LOG_ERR, "Error setting up SIGHUP handler: %s", strerror(errno));
    }
}

void Daemon::writePidFile() {
    std::ofstream pidFile(pidFilePath_, std::ios::trunc);
    if (pidFile.is_open()) {
        pidFile << getpid() << "\n";
    } else {
        syslog(LOG_ERR, "Error: Failed to write PID to file %s", pidFilePath_.c_str());
    }
}

void Daemon::removePidFile() {
    unlink(pidFilePath_.c_str());
}

void Daemon::run() {
    try {
        auto config = ConfigParser::load(configPath_);
        syslog(LOG_INFO, "Configuration loaded. Watching directories: %zu", config.watchDirs.size());
        
        Worker worker;
        worker.init(config.watchDirs);

        while (g_running) {
            if (g_reload) {
                syslog(LOG_INFO, "Received SIGHUP signal. Reloading configuration...");
                g_reload = 0;
                try {
                    config = ConfigParser::load(configPath_);
                    // worker.init(config.watchDirs);
                    syslog(LOG_INFO, "Configuration successfully updated");
                } catch (const std::exception& e) {
                    syslog(LOG_ERR, "Error updating configuration: %s", e.what());
                }
            }

            worker.pollEvents();
        }

    } catch (const std::exception& e) {
        syslog(LOG_ERR, "Critical error: %s", e.what());
    }

    syslog(LOG_INFO, "Completion of daemon work (SIGTERM)");
    removePidFile();
    closelog();
}

} // namespace monitor