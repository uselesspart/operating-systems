#include "PidFile.h"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#include <signal.h>

#include <chrono>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <string>
#include <thread>

namespace disk_monitor {
namespace {

std::optional<pid_t> readPid(const std::filesystem::path& path) {
    std::ifstream input(path);
    long long value = 0;
    if (!(input >> value) || value <= 0) {
        return std::nullopt;
    }
    return static_cast<pid_t>(value);
}

bool isDiskMonitorProcess(const pid_t pid) {
    std::ifstream command_name("/proc/" + std::to_string(pid) + "/comm");
    std::string name;
    if (command_name >> name && name == "disk_monitor") {
        return true;
    }

    std::ifstream command_line("/proc/" + std::to_string(pid) + "/cmdline");
    std::string contents((std::istreambuf_iterator<char>(command_line)),
                         std::istreambuf_iterator<char>());
    return contents.find("disk_monitor") != std::string::npos;
}

}  // namespace

std::filesystem::path PidFile::defaultPath() {
    if (const char* configured = std::getenv("DISK_MONITOR_PIDFILE"); configured != nullptr &&
        *configured != '\0') {
        return configured;
    }
    return "/tmp/disk_monitor.pid";
}

bool PidFile::processExists(const pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    struct stat process_directory {};
    if (stat(("/proc/" + std::to_string(pid)).c_str(), &process_directory) != 0 ||
        !S_ISDIR(process_directory.st_mode)) {
        return false;
    }

    std::ifstream process_stat("/proc/" + std::to_string(pid) + "/stat");
    std::string stat_line;
    if (!std::getline(process_stat, stat_line)) {
        return true;
    }
    const auto closing_name = stat_line.rfind(')');
    return closing_name == std::string::npos ||
           closing_name + 2 >= stat_line.size() || stat_line[closing_name + 2] != 'Z';
}

bool PidFile::stopPrevious(const std::filesystem::path& path, Logger& logger) {
    const auto previous_pid = readPid(path);
    if (!previous_pid.has_value()) {
        if (std::filesystem::exists(path)) {
            std::error_code error;
            std::filesystem::remove(path, error);
        }
        return true;
    }

    if (!processExists(*previous_pid)) {
        std::error_code error;
        std::filesystem::remove(path, error);
        return true;
    }

    if (!isDiskMonitorProcess(*previous_pid)) {
        logger.error("pid file points to another process; refusing to terminate it");
        return false;
    }

    if (kill(*previous_pid, SIGTERM) < 0 && errno != ESRCH) {
        logger.error("cannot stop previous daemon: " + std::string(std::strerror(errno)));
        return false;
    }

    for (int attempt = 0; attempt < 100; ++attempt) {
        if (!processExists(*previous_pid)) {
            std::error_code error;
            std::filesystem::remove(path, error);
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    logger.error("previous daemon did not stop in time");
    return false;
}

PidFile::~PidFile() {
    if (descriptor_ >= 0) {
        close(descriptor_);
        std::error_code error;
        std::filesystem::remove(path_, error);
    }
}

bool PidFile::acquire(const std::filesystem::path& path, Logger& logger) {
    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) {
            logger.error("cannot create pid directory: " + error.message());
            return false;
        }
    }

    descriptor_ = open(path.c_str(), O_RDWR | O_CREAT, 0644);
    if (descriptor_ < 0) {
        logger.error("cannot open pid file '" + path.string() + "': " + std::strerror(errno));
        return false;
    }
    if (flock(descriptor_, LOCK_EX | LOCK_NB) < 0) {
        logger.error("another daemon owns pid file '" + path.string() + "'");
        close(descriptor_);
        descriptor_ = -1;
        return false;
    }

    if (ftruncate(descriptor_, 0) < 0) {
        logger.error("cannot truncate pid file: " + std::string(std::strerror(errno)));
        return false;
    }
    const std::string pid_text = std::to_string(getpid()) + "\n";
    const ssize_t written = write(descriptor_, pid_text.data(), pid_text.size());
    if (written != static_cast<ssize_t>(pid_text.size())) {
        logger.error("cannot write pid file: " + std::string(std::strerror(errno)));
        return false;
    }
    path_ = path;
    return true;
}

}  // namespace disk_monitor

