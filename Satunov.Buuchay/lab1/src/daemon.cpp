#include "daemon.hpp"

#include "cleaner.hpp"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cerrno>
#include <fcntl.h>
#include <fstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <syslog.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

volatile std::sig_atomic_t Daemon::terminate_requested_ = 0;
volatile std::sig_atomic_t Daemon::reload_requested_ = 0;

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

void Daemon::signal_handler(int signal_number) {
    if (signal_number == SIGTERM) {
        terminate_requested_ = 1;
    } else if (signal_number == SIGHUP) {
        reload_requested_ = 1;
    }
}

bool Daemon::daemonize() const {
    const pid_t pid = fork();

    if (pid < 0) {
        return false;
    }

    if (pid > 0) {
        std::_Exit(EXIT_SUCCESS);
    }

    if (setsid() < 0 || chdir("/") < 0) {
        return false;
    }

    umask(0);

    const int null_fd = open("/dev/null", O_RDWR);

    if (null_fd < 0) {
        return false;
    }

    if (dup2(null_fd, STDIN_FILENO) < 0 ||
        dup2(null_fd, STDOUT_FILENO) < 0 ||
        dup2(null_fd, STDERR_FILENO) < 0) {
        close(null_fd);
        return false;
    }

    if (null_fd > STDERR_FILENO) {
        close(null_fd);
    }

    return true;
}

bool Daemon::is_process_running(pid_t pid) const {
    if (pid <= 0) {
        return false;
    }

    if (kill(pid, 0) == 0) {
        return true;
    }

    return errno == EPERM;
}

bool Daemon::is_our_process(pid_t pid) const {
    std::error_code error;
    const fs::path current_executable =
        fs::read_symlink("/proc/self/exe", error);

    if (error) {
        return false;
    }

    const fs::path other_executable =
        fs::read_symlink(
            "/proc/" + std::to_string(pid) + "/exe",
            error
        );

    return !error && current_executable == other_executable;
}

bool Daemon::stop_previous_instance() const {
    std::ifstream input(kPidFile);
    pid_t old_pid = 0;

    if (!(input >> old_pid)) {
        return true;
    }

    if (!is_process_running(old_pid)) {
        return true;
    }

    if (!is_our_process(old_pid)) {
        syslog(
            LOG_WARNING,
            "PID file belongs to another process: %d",
            old_pid
        );
        return true;
    }

    syslog(LOG_INFO, "Stopping previous daemon: %d", old_pid);

    if (kill(old_pid, SIGTERM) < 0 && errno != ESRCH) {
        syslog(LOG_ERR, "Cannot stop previous daemon");
        return false;
    }

    for (int attempt = 0; attempt < 50; ++attempt) {
        if (!is_process_running(old_pid)) {
            return true;
        }

        std::this_thread::sleep_for(
            std::chrono::milliseconds(100)
        );
    }

    syslog(LOG_ERR, "Previous daemon did not stop");
    return false;
}

bool Daemon::write_pid_file() const {
    std::ofstream output(kPidFile);

    if (!output) {
        return false;
    }

    output << getpid() << '\n';
    return true;
}

void Daemon::remove_pid_file() const {
    std::ifstream input(kPidFile);
    pid_t pid_in_file = 0;

    if ((input >> pid_in_file) && pid_in_file == getpid()) {
        std::error_code error;
        fs::remove(kPidFile, error);
    }
}

void Daemon::run_loop() {
    while (!terminate_requested_) {
        try {
            for (const ConfigEntry& entry : config_.entries()) {
                Cleaner::clean(entry.folder, entry.depth);
            }
        } catch (const std::exception& error) {
            syslog(LOG_ERR, "Cleaning error: %s", error.what());
        }

        for (int second = 0;
             second < 20 && !terminate_requested_;
             ++second) {
            if (reload_requested_) {
                reload_requested_ = 0;

                try {
                    Config new_config = Config::load(config_path_);
                    config_ = new_config;
                    syslog(LOG_INFO, "Configuration reloaded");
                } catch (const std::exception& error) {
                    syslog(
                        LOG_ERR,
                        "Configuration reload failed: %s",
                        error.what()
                    );
                }
            }

            std::this_thread::sleep_for(
                std::chrono::seconds(1)
            );
        }
    }
}

int Daemon::run(const fs::path& config_path) {
    config_path_ = fs::absolute(config_path);

    openlog(
        "os_lab1",
        LOG_PID | LOG_NDELAY,
        LOG_DAEMON
    );

    if (!stop_previous_instance() || !daemonize()) {
        closelog();
        return 1;
    }

    if (!write_pid_file()) {
        syslog(LOG_ERR, "Cannot write PID file");
        closelog();
        return 1;
    }

    try {
        config_ = Config::load(config_path_);

        std::signal(SIGTERM, signal_handler);
        std::signal(SIGHUP, signal_handler);

        syslog(LOG_INFO, "Daemon started");
        run_loop();
        syslog(LOG_INFO, "Daemon stopped");

        remove_pid_file();
        closelog();
        return 0;
    } catch (const std::exception& error) {
        syslog(LOG_ERR, "Daemon error: %s", error.what());
        remove_pid_file();
        closelog();
        return 1;
    }
}