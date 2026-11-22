#include "Daemon.hpp"

#include <cerrno>
#include <exception>
#include <fcntl.h>
#include <fstream>
#include <poll.h>
#include <signal.h>
#include <string>
#include <system_error>
#include <utility>

#include <sys/signalfd.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

namespace application {

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

bool Daemon::reload() {
    auto config = load_config(config_path_, start_dir_);
    if (!config) {
        ::syslog(LOG_ERR, "cannot load config: %s", config_path_.c_str());
        return false;
    }

    try {
        if (monitor_) {
            auto next = std::make_unique<Monitor>(std::move(*config));
            monitor_.swap(next);
        } else {
            config_ = std::move(*config);
        }
    } catch (const std::exception& error) {
        ::syslog(LOG_ERR, "cannot start monitor: %s", error.what());
        return false;
    }

    ::syslog(LOG_INFO, "config loaded: %s", config_path_.c_str());
    return true;
}

bool Daemon::stop_previous() {
    if (!std::filesystem::exists(pid_path_)) {
        return true;
    }

    std::ifstream file{pid_path_};
    if (!file) {
        ::syslog(LOG_ERR, "cannot read pid file: %s", pid_path_.c_str());
        return false;
    }

    pid_t old_pid = 0;
    if (!(file >> old_pid) || old_pid <= 1) {
        ::syslog(LOG_WARNING, "invalid pid file: %s", pid_path_.c_str());
        return true;
    }

    const Path proc_dir = Path{"/proc"} / std::to_string(old_pid);
    if (!std::filesystem::exists(proc_dir)) {
        return true;
    }

    if (::kill(old_pid, SIGTERM) == -1) {
        if (errno == ESRCH) {
            return true;
        }
        ::syslog(LOG_ERR, "cannot stop pid %ld: %m", static_cast<long>(old_pid));
        return false;
    }

    for (int attempt = 0; attempt < 50; ++attempt) {
        if (!std::filesystem::exists(proc_dir)) {
            return true;
        }
        ::usleep(100000);
    }

    ::syslog(LOG_ERR, "pid %ld did not exit", static_cast<long>(old_pid));
    return false;
}

bool Daemon::write_pid() {
    int fd = ::open(pid_path_.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd == -1) {
        ::syslog(LOG_ERR, "cannot open pid file: %m");
        return false;
    }

    const std::string text = std::to_string(::getpid()) + "\n";
    const ssize_t written = ::write(fd, text.data(), text.size());
    const int saved_error = errno;
    ::close(fd);
    if (written != static_cast<ssize_t>(text.size())) {
        errno = saved_error;
        ::syslog(LOG_ERR, "cannot write pid file: %m");
        return false;
    }
    return true;
}

void Daemon::remove_pid() {
    std::ifstream file{pid_path_};
    pid_t stored_pid = 0;
    if (file >> stored_pid && stored_pid == ::getpid()) {
        ::unlink(pid_path_.c_str());
    }
}

int Daemon::event_loop() {
    for (;;) {
        pollfd fds[2] = {
            {monitor_->fd(), POLLIN, 0},
            {signal_fd_, POLLIN, 0}
        };

        if (::poll(fds, 2, -1) == -1) {
            if (errno == EINTR) {
                continue;
            }
            ::syslog(LOG_ERR, "poll failed: %m");
            return 1;
        }

        if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            ::syslog(LOG_ERR, "signal descriptor is unavailable");
            return 1;
        }
        if (fds[1].revents & POLLIN) {
            for (;;) {
                signalfd_siginfo info{};
                const ssize_t size = ::read(signal_fd_, &info, sizeof(info));
                if (size == -1 && errno == EAGAIN) {
                    break;
                }
                if (size != static_cast<ssize_t>(sizeof(info))) {
                    ::syslog(LOG_ERR, "cannot read signal: %m");
                    return 1;
                }
                if (info.ssi_signo == SIGTERM) {
                    return 0;
                }
                if (info.ssi_signo == SIGHUP) {
                    reload();
                }
            }
            continue;
        }

        if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
            ::syslog(LOG_ERR, "inotify descriptor is unavailable");
            return 1;
        }
        if ((fds[0].revents & POLLIN) && !monitor_->read_events()) {
            return 1;
        }
    }
}

int Daemon::run(Arguments args) {
    std::error_code error;
    config_path_ = std::filesystem::absolute(args.config, error);
    if (error) {
        ::syslog(LOG_ERR, "cannot resolve config path");
        return 1;
    }
    start_dir_ = std::filesystem::current_path(error);
    if (error) {
        ::syslog(LOG_ERR, "cannot read working directory");
        return 1;
    }
    pid_path_ = Path{"/tmp"} / ("disk-monitor-" + std::to_string(::getuid()) + ".pid");

    if (!reload()) {
        return 1;
    }

    if (!stop_previous()) {
        return 1;
    }

    sigset_t signals;
    sigemptyset(&signals);
    sigaddset(&signals, SIGHUP);
    sigaddset(&signals, SIGTERM);
    if (::sigprocmask(SIG_BLOCK, &signals, nullptr) == -1) {
        ::syslog(LOG_ERR, "cannot block signals: %m");
        return 1;
    }

    pid_t child = ::fork();
    if (child == -1) {
        ::syslog(LOG_ERR, "fork failed: %m");
        return 1;
    }
    if (child > 0) {
        return 0;
    }

    if (::setsid() == -1 || ::chdir("/") == -1) {
        ::syslog(LOG_ERR, "daemonization failed: %m");
        return 1;
    }
    ::umask(027);

    ::closelog();
    long max_fd = ::sysconf(_SC_OPEN_MAX);
    if (max_fd < 0) {
        max_fd = 1024;
    }
    for (long fd = 0; fd < max_fd; ++fd) {
        ::close(static_cast<int>(fd));
    }

    int null_fd = ::open("/dev/null", O_RDWR);
    if (null_fd == -1 || ::dup2(null_fd, 0) == -1 ||
        ::dup2(null_fd, 1) == -1 || ::dup2(null_fd, 2) == -1) {
        ::openlog("disk-monitor", LOG_PID, LOG_LOCAL0);
        ::syslog(LOG_ERR, "cannot redirect standard streams: %m");
        return 1;
    }
    if (null_fd > 2) {
        ::close(null_fd);
    }
    ::openlog("disk-monitor", LOG_PID, LOG_LOCAL0);

    try {
        monitor_ = std::make_unique<Monitor>(std::move(config_));
    } catch (const std::exception& error) {
        ::syslog(LOG_ERR, "cannot start monitor: %s", error.what());
        return 1;
    }

    signal_fd_ = ::signalfd(-1, &signals, SFD_NONBLOCK | SFD_CLOEXEC);
    if (signal_fd_ == -1 || !write_pid()) {
        ::syslog(LOG_ERR, "daemon startup failed: %m");
        return 1;
    }

    ::syslog(LOG_INFO, "daemon started, pid=%ld", static_cast<long>(::getpid()));
    const int result = event_loop();
    ::syslog(result == 0 ? LOG_INFO : LOG_ERR, "daemon stopped");
    remove_pid();
    ::close(signal_fd_);
    signal_fd_ = -1;
    return result;
}

} // namespace application

