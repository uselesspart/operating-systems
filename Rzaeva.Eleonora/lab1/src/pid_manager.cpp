#include "pid_manager.hpp"

#include <chrono>
#include <csignal>
#include <fstream>
#include <signal.h>
#include <string>
#include <thread>

namespace fs = std::filesystem;

namespace dirclean {

PidManager::PidManager(fs::path pidFile) : pidFile_(std::move(pidFile)) {}

long PidManager::read() const {
    std::ifstream in(pidFile_);
    if (!in.is_open()) return 0;
    long pid = 0;
    if (!(in >> pid)) return 0;
    return pid > 0 ? pid : 0;
}

void PidManager::write(long pid) const {
    std::ofstream out(pidFile_, std::ios::trunc);
    out << pid << '\n';
}

void PidManager::remove() const {
    std::error_code ec;
    fs::remove(pidFile_, ec);
}

bool PidManager::processAlive(long pid) {
    if (pid <= 0) return false;

    const std::string statPath = "/proc/" + std::to_string(pid) + "/stat";
    std::ifstream in(statPath);
    if (!in.is_open()) return false;

    std::string content;
    std::getline(in, content);
    const std::size_t close = content.rfind(')');
    if (close == std::string::npos || close + 2 >= content.size()) return false;

    const char state = content[close + 2];
    return state != 'Z' && state != 'X';
}

long PidManager::stopPrevious(long timeoutMs) const {
    const long pid = read();
    if (pid <= 0 || !processAlive(pid)) return 0;

    if (::kill(static_cast<pid_t>(pid), SIGTERM) != 0) return 0;

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (!processAlive(pid)) return pid;
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    return pid;
}

}
