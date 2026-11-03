#include "pid_manager.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <fstream>
#include <thread>
#include <unistd.h>

PidManager::PidManager(std::string pidFile) : pidFile_(std::move(pidFile)) {}

std::string PidManager::defaultPath() {
    const char* env = std::getenv("DIRCLEAN_PID_FILE");
    return (env && *env) ? env : "/tmp/dirclean_daemon.pid";
}

bool PidManager::readPid(pid_t& pid) const {
    std::ifstream in(pidFile_);
    long value = 0;
    if (!(in >> value) || value <= 0) return false;
    pid = static_cast<pid_t>(value);
    return true;
}

bool PidManager::writePid(pid_t pid) const {
    std::ofstream out(pidFile_, std::ios::trunc);
    if (!out) return false;
    out << pid << '\n';
    return static_cast<bool>(out);
}

void PidManager::removeIfOwnedBy(pid_t pid) const {
    pid_t stored = 0;
    if (readPid(stored) && stored == pid) unlink(pidFile_.c_str());
}

bool PidManager::processExists(pid_t pid) {
    if (pid <= 0) return false;
    std::ifstream stat("/proc/" + std::to_string(pid) + "/stat");
    if (!stat) return false;
    std::string line;
    std::getline(stat, line);
    // формат: "pid (comm) S ..." — состояние идёт после последней ')'
    const auto p = line.rfind(')');
    if (p != std::string::npos && p + 2 < line.size() && line[p + 2] == 'Z') return false;
    return true;
}

bool PidManager::stopOldInstance(int timeoutSeconds) const {
    pid_t old = 0;
    if (!readPid(old) || old == getpid() || !processExists(old)) return true;

    if (kill(old, SIGTERM) != 0 && errno != ESRCH) return false;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeoutSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!processExists(old)) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return !processExists(old);
}
