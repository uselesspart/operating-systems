#include "Daemon.h"

#include "Config.h"
#include "FileWatcher.h"
#include "Logger.h"
#include "PidFile.h"
#include "UniqueFd.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <cstdlib>
#include <fcntl.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/signalfd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace disk_monitor {
namespace {

bool blockSignals() {
    sigset_t mask;
    if (sigemptyset(&mask) == -1) {
        return false;
    }
    if (sigaddset(&mask, SIGTERM) == -1 || sigaddset(&mask, SIGINT) == -1 ||
        sigaddset(&mask, SIGHUP) == -1) {
        return false;
    }
    return sigprocmask(SIG_BLOCK, &mask, nullptr) == 0;
}

bool daemonizeProcess() {
    const pid_t first_child = fork();
    if (first_child < 0) {
        return false;
    }
    if (first_child > 0) {
        _exit(EXIT_SUCCESS);
    }

    if (setsid() < 0) {
        return false;
    }

    const pid_t second_child = fork();
    if (second_child < 0) {
        return false;
    }
    if (second_child > 0) {
        _exit(EXIT_SUCCESS);
    }

    umask(027);
    if (chdir("/") < 0) {
        return false;
    }

    const int null_descriptor = open("/dev/null", O_RDWR);
    if (null_descriptor < 0) {
        return false;
    }
    if (dup2(null_descriptor, STDIN_FILENO) < 0 || dup2(null_descriptor, STDOUT_FILENO) < 0 ||
        dup2(null_descriptor, STDERR_FILENO) < 0) {
        close(null_descriptor);
        return false;
    }
    if (null_descriptor > STDERR_FILENO) {
        close(null_descriptor);
    }
    return true;
}

UniqueFd createSignalFd() {
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGTERM);
    sigaddset(&mask, SIGINT);
    sigaddset(&mask, SIGHUP);
    return UniqueFd(signalfd(-1, &mask, SFD_CLOEXEC | SFD_NONBLOCK));
}

}  // namespace

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

int Daemon::run(const std::filesystem::path& config_path) {
    const auto absolute_config_path = ConfigParser::absoluteConfigPath(config_path);
    const auto pid_path = PidFile::defaultPath();

    Logger logger;
    if (!PidFile::stopPrevious(pid_path, logger)) {
        return 1;
    }
    if (!blockSignals()) {
        return 1;
    }
    if (!daemonizeProcess()) {
        return 1;
    }

    logger.open();
    logger.info("daemon started");

    PidFile pid_file;
    if (!pid_file.acquire(pid_path, logger)) {
        logger.error("cannot acquire pid file");
        return 1;
    }

    Config config;
    try {
        config = ConfigParser::load(absolute_config_path);
    } catch (const std::exception& error) {
        logger.error(error.what());
        return 1;
    }

    try {
        FileWatcher watcher(logger);
        watcher.reload(config.directories);
        logger.info("watching " + std::to_string(config.directories.size()) + " configured director" +
                    (config.directories.size() == 1 ? "y" : "ies"));

        UniqueFd signal_fd = createSignalFd();
        if (!signal_fd) {
            logger.error(std::string("cannot create signalfd: ") + std::strerror(errno));
            return 1;
        }

        bool stopping = false;
        while (!stopping) {
            pollfd descriptors[2] = {{watcher.descriptor(), POLLIN, 0}, {signal_fd.get(), POLLIN, 0}};
            const int poll_result = poll(descriptors, 2, -1);
            if (poll_result < 0) {
                if (errno == EINTR) {
                    continue;
                }
                logger.error(std::string("poll failed: ") + std::strerror(errno));
                break;
            }

            if ((descriptors[0].revents & POLLIN) != 0) {
                watcher.consumeEvents();
            }

            if ((descriptors[1].revents & POLLIN) == 0) {
                continue;
            }

            signalfd_siginfo signal_info{};
            while (read(signal_fd.get(), &signal_info, sizeof(signal_info)) ==
                   static_cast<ssize_t>(sizeof(signal_info))) {
                if (signal_info.ssi_signo == SIGHUP) {
                    try {
                        config = ConfigParser::load(absolute_config_path);
                        watcher.reload(config.directories);
                        logger.info("configuration reloaded");
                    } catch (const std::exception& error) {
                        logger.error(std::string("configuration reload failed: ") + error.what());
                    }
                } else if (signal_info.ssi_signo == SIGTERM || signal_info.ssi_signo == SIGINT) {
                    logger.info("daemon stopping after termination signal");
                    stopping = true;
                    break;
                }
            }
        }
    } catch (const std::exception& error) {
        logger.error(std::string("cannot initialize file watcher: ") + error.what());
        return 1;
    }

    logger.info("daemon stopped");
    return 0;
}

}  // namespace disk_monitor

