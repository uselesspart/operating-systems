#include "pid_manager.hpp"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <fstream>
#include <iterator>
#include <sys/file.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

namespace dirclean {

PidManager::PidManager(std::string pidFile, std::string procRoot)
    : pidFile_(std::move(pidFile)), procRoot_(std::move(procRoot)) {}

PidManager::~PidManager() { releaseStartupLock(); }

bool PidManager::acquireStartupLock() {
    if (lockFd_ >= 0) return true;

    std::string lockPath = pidFile_ + ".lock";
    lockFd_ = open(lockPath.c_str(), O_CREAT | O_RDWR, 0666);
    if (lockFd_ < 0) return false;
    if (flock(lockFd_, LOCK_EX) == 0) return true;

    int savedErrno = errno;
    close(lockFd_);
    lockFd_ = -1;
    errno = savedErrno;
    return false;
}

void PidManager::releaseStartupLock() {
    if (lockFd_ < 0) return;
    flock(lockFd_, LOCK_UN);
    close(lockFd_);
    lockFd_ = -1;
}

std::optional<long> PidManager::readPid() const {
    std::ifstream in(pidFile_);
    long pid = 0;
    if (!(in >> pid) || pid <= 0) return std::nullopt;
    return pid;
}

bool PidManager::isProcessAlive(long pid) const {
    std::ifstream in(procRoot_ + "/" + std::to_string(pid) + "/stat");
    if (!in) return false;
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::size_t pos = content.rfind(')');
    if (pos == std::string::npos || pos + 2 >= content.size()) return false;
    char state = content[pos + 2];
    return state != 'Z' && state != 'X';
}

TerminateResult PidManager::terminatePrevious(std::chrono::milliseconds timeout) const {
    TerminateResult res;
    std::optional<long> pid = readPid();
    if (!pid || *pid == static_cast<long>(getpid()) || !isProcessAlive(*pid)) return res;

    res.pid = *pid;
    if (kill(static_cast<pid_t>(*pid), SIGTERM) != 0) {
        if (errno != ESRCH) {
            res.status = TerminateStatus::SignalFailed;
            res.errorCode = errno;
        }
        return res;
    }

    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (isProcessAlive(*pid)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            res.status = TerminateStatus::StillRunning;
            return res;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    res.status = TerminateStatus::Terminated;
    return res;
}

bool PidManager::write(pid_t pid) const {
    std::ofstream out(pidFile_, std::ios::trunc);
    if (!out) return false;
    out << pid << std::endl;
    return static_cast<bool>(out);
}

void PidManager::removeIfOwner(pid_t pid) const {
    std::optional<long> current = readPid();
    if (current && *current == static_cast<long>(pid)) unlink(pidFile_.c_str());
}

} 
