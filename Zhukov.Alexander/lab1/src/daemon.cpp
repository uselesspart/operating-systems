#include "daemon.h"
#include "logger.h"
#include "pid_file.h"

#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#include <dirent.h>
#include <poll.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <syslog.h>

namespace {
    using Clock = std::chrono::steady_clock;
    volatile sig_atomic_t stop_requested = 0;
    volatile sig_atomic_t reload_pending = 0;

    struct Startup {
        pid_t pid;
        int code;
        char error[384];
    };

    void signal_handler(int signal) {
        if (signal == SIGTERM) {
            stop_requested = 1;
        } else if (signal == SIGHUP) {
            reload_pending = 1;
        }
    }

    sigset_t control_signals() {
        sigset_t signals;
        sigemptyset(&signals);
        sigaddset(&signals, SIGHUP);
        sigaddset(&signals, SIGTERM);
        return signals;
    }

    std::filesystem::path state_directory() {
        const char* override_path = std::getenv("OS_LAB_STATE_DIR");
        if (override_path != nullptr) {
            const std::filesystem::path path(override_path);
            if (path.empty() || !path.is_absolute()) {
                throw std::runtime_error("Invalid state directory");
            }
            return path;
        }
        const char* home = std::getenv("HOME");
        if (home == nullptr || !std::filesystem::path(home).is_absolute()) {
            throw std::runtime_error("Cannot find home directory");
        }
        return std::filesystem::path(home) / ".local/state/os-lab1";
    }

    bool send_startup(int fd, int code, const std::string& error = {}) {
        Startup reply{};
        reply.pid = getpid();
        reply.code = code;
        std::memcpy(reply.error, error.data(), std::min(error.size(), sizeof(reply.error) - 1));
        ssize_t size;
        do {
            size = write(fd, &reply, sizeof(reply));
        } while (size < 0 && errno == EINTR);
        return size == static_cast<ssize_t>(sizeof(reply));
    }

    bool wait_child(pid_t pid) {
        int status;
        const auto deadline = Clock::now() + std::chrono::seconds(1);
        while (Clock::now() < deadline) {
            const auto result = waitpid(pid, &status, WNOHANG);
            if (result == pid || (result < 0 && errno == ECHILD)) {
                return true;
            }
            if (result < 0 && errno != EINTR) {
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    void cancel_start(pid_t pid) {
        kill(-pid, SIGTERM);
        kill(pid, SIGTERM);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        kill(-pid, SIGKILL);
        kill(pid, SIGKILL);
        wait_child(pid);
    }

    void wait_startup(int fd) {
        Startup reply{};
        std::size_t offset = 0;
        const auto deadline = Clock::now() + std::chrono::seconds(5);
        pollfd descriptor{fd, POLLIN, 0};
        while (offset < sizeof(reply)) {
            if (Clock::now() >= deadline) {
                throw std::runtime_error("Monitor start timeout");
            }
            const int ready = poll(&descriptor, 1, 100);
            if (ready < 0) {
                if (errno == EINTR) {
                    continue;
                }
                throw std::system_error(errno, std::generic_category(), "Cannot wait for startup");
            }
            if (ready == 0) {
                continue;
            }
            const auto size = read(fd, reinterpret_cast<char*>(&reply) + offset, sizeof(reply) - offset);
            if (size < 0 && (errno == EAGAIN || errno == EINTR)) {
                continue;
            }
            if (size <= 0) {
                throw std::runtime_error("Monitor start failed");
            }
            offset += static_cast<std::size_t>(size);
        }
        reply.error[sizeof(reply.error) - 1] = '\0';
        if (reply.code != 0 || reply.pid <= 1) {
            throw std::runtime_error(reply.error[0] != '\0' ? reply.error : "Monitor start failed");
        }
    }

    void close_descriptors(const std::vector<int>& keep) {
        DIR* directory = opendir("/proc/self/fd");
        if (directory == nullptr) {
            throw std::system_error(errno, std::generic_category(), "Cannot list descriptors");
        }
        while (const auto entry = readdir(directory)) {
            char* end = nullptr;
            const long fd = std::strtol(entry->d_name, &end, 10);
            if (*end == '\0' && fd >= 3 && fd != dirfd(directory) &&
                std::find(keep.begin(), keep.end(), fd) == keep.end()) {
                close(static_cast<int>(fd));
            }
        }
        closedir(directory);
    }

    void redirect_streams() {
        Fd file(open("/dev/null", O_RDWR));
        if (file.get() < 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot open /dev/null");
        }
        for (int fd = 0; fd <= 2; ++fd) {
            if (dup2(file.get(), fd) < 0) {
                throw std::system_error(errno, std::generic_category(), "Cannot redirect output");
            }
        }
        if (file.get() < 3) {
            file.release();
        }
    }

    void set_signals() {
        struct sigaction action{};
        action.sa_handler = signal_handler;
        action.sa_mask = control_signals();
        if (sigaction(SIGTERM, &action, nullptr) != 0 ||
            sigaction(SIGHUP, &action, nullptr) != 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot set signals");
        }
        action.sa_handler = SIG_IGN;
        if (sigaction(SIGPIPE, &action, nullptr) != 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot ignore SIGPIPE");
        }
        const auto signals = control_signals();
        if (sigprocmask(SIG_UNBLOCK, &signals, nullptr) != 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot unblock signals");
        }
    }
}

Daemon::Daemon() = default;
Daemon::~Daemon() = default;

int Daemon::start() {
    umask(0077);
    pid_file = std::make_unique<PidFile>(state_directory());
    pid_file->stop_previous();
    int pipe[2];
    if (pipe2(pipe, O_CLOEXEC | O_NONBLOCK) != 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot create startup pipe");
    }
    Fd reader(pipe[0]);
    startup_pipe.reset(pipe[1]);
    reader.move_up();
    startup_pipe.move_up();
    std::cout.flush();
    std::cerr.flush();
    const pid_t child = fork();
    if (child < 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot fork");
    }
    if (child > 0) {
        startup_pipe.reset();
        try {
            wait_startup(reader.get());
            if (!wait_child(child)) {
                throw std::runtime_error("Cannot wait for child");
            }
        } catch (...) {
            cancel_start(child);
            pid_file.reset();
            throw;
        }
        pid_file.reset();
        std::cout << "Monitoring started\n";
        return EXIT_SUCCESS;
    }
    reader.reset();
    try {
        stop_requested = 0;
        reload_pending = 0;
        set_signals();
        if (setsid() < 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot create session");
        }
        const pid_t pid = fork();
        if (pid < 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot fork");
        }
        if (pid > 0) {
            _exit(EXIT_SUCCESS);
        }
        if (chdir("/") != 0) {
            throw std::system_error(errno, std::generic_category(), "Cannot change directory");
        }
        closelog();
        close_descriptors({startup_pipe.get(), pid_file->directory_fd(), pid_file->lock_fd()});
        redirect_streams();
        Logger logger;
        try {
            work();
            Logger::info("Monitoring stopped");
            pid_file.reset();
        } catch (...) {
            pid_file.reset();
            throw;
        }
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        Logger logger;
        Logger::error(error.what());
        if (startup_pipe.get() >= 0) {
            send_startup(startup_pipe.get(), EXIT_FAILURE, error.what());
            startup_pipe.reset();
        }
        pid_file.reset();
        return EXIT_FAILURE;
    }
}

void Daemon::ready() {
    if (stopping()) {
        throw std::runtime_error("Monitor start interrupted");
    }
    pid_file->save();
    Logger::info("Monitoring started");
    if (!send_startup(startup_pipe.get(), EXIT_SUCCESS)) {
        throw std::runtime_error("Cannot report startup");
    }
    startup_pipe.reset();
    pid_file->release_lock();
}

bool Daemon::stopping() { return stop_requested != 0; }

bool Daemon::reload_requested() {
    const auto signals = control_signals();
    sigset_t saved;
    if (sigprocmask(SIG_BLOCK, &signals, &saved) != 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot block signals");
    }
    const bool requested = reload_pending != 0 && stop_requested == 0;
    reload_pending = 0;
    if (sigprocmask(SIG_SETMASK, &saved, nullptr) != 0) {
        throw std::system_error(errno, std::generic_category(), "Cannot restore signals");
    }
    return requested && !stopping();
}
