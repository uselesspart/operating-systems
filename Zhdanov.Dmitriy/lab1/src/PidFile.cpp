#include "PidFile.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <thread>
#include <unistd.h>

static bool processIsAlive(pid_t pid) {
    std::ifstream status("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    if (std::getline(status, line)) {
        std::size_t close = line.rfind(')');
        if (close != std::string::npos && close + 2 < line.size()) {
            return line[close + 2] != 'Z';
        }
    }
    return kill(pid, 0) == 0 || errno == EPERM;
}

static bool isReminderProcess(pid_t pid) {
    std::ifstream comm("/proc/" + std::to_string(pid) + "/comm");
    std::string name;
    if (!std::getline(comm, name)) {
        return false;
    }

    std::ifstream self("/proc/self/comm");
    std::string selfName;
    if (!std::getline(self, selfName)) {
        return false;
    }
    return name == selfName;
}

std::string pidFilePath() {
    const char* runtime = std::getenv("XDG_RUNTIME_DIR");
    if (runtime != nullptr && *runtime != '\0') {
        return std::string(runtime) + "/reminder_daemon.pid";
    }
    return "/tmp/reminder_daemon.pid";
}

bool stopRunningInstance(std::string& error) {
    std::string path = pidFilePath();
    std::ifstream input(path);
    if (!input) {
        return true;
    }

    long long value = 0;
    if (!(input >> value) || value <= 0) {
        std::remove(path.c_str());
        return true;
    }

    pid_t pid = static_cast<pid_t>(value);
    if (!processIsAlive(pid) || !isReminderProcess(pid)) {
        std::remove(path.c_str());
        return true;
    }

    if (kill(pid, SIGTERM) != 0 && errno != ESRCH) {
        error = "не удалось остановить предыдущий экземпляр: " + std::string(std::strerror(errno));
        return false;
    }

    for (int attempt = 0; attempt < 100; ++attempt) {
        if (!processIsAlive(pid)) {
            std::remove(path.c_str());
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    error = "предыдущий экземпляр не завершился по SIGTERM";
    return false;
}

bool writePidFile(std::string& error) {
    std::string path = pidFilePath();
    std::ofstream output(path, std::ios::trunc);
    if (!output) {
        error = "не удалось открыть " + path;
        return false;
    }
    output << getpid() << '\n';
    if (!output) {
        error = "не удалось записать " + path;
        return false;
    }
    return true;
}

void removePidFile() {
    std::remove(pidFilePath().c_str());
}
