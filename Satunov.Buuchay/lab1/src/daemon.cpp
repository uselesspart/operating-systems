#include "daemon.hpp"

#include "cleaner.hpp"
#include "config.hpp"

#include <chrono>
#include <cstdlib>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>
#include <thread>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

    constexpr int kIntervalSeconds = 20;

    void process_config(const Config& config) {
        for (const ConfigEntry& entry : config.entries()) {
            Cleaner::clean(entry.folder, entry.depth);
        }
    }

} // namespace

bool Daemon::daemonize() {
    const pid_t pid = fork();

    if (pid < 0) {
        return false;
    }

    if (pid > 0) {
        std::_Exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        return false;
    }

    umask(0);

    if (chdir("/") < 0) {
        return false;
    }

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

int Daemon::run(const fs::path& config_path) {
    if (!daemonize()) {
        return 1;
    }

    openlog(
        "os_lab1",
        LOG_PID | LOG_NDELAY,
        LOG_DAEMON
    );

    syslog(LOG_INFO, "Daemon started");

    try {
        const Config config = Config::load(config_path);

        while (true) {
            process_config(config);

            std::this_thread::sleep_for(
                std::chrono::seconds(kIntervalSeconds)
            );
        }
    } catch (const std::exception& error) {
        syslog(LOG_ERR, "Daemon error: %s", error.what());
        closelog();
        return 1;
    }
}