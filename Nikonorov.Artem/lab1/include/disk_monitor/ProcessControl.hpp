#pragma once

#include <filesystem>
#include <string>
#include <sys/types.h>

namespace diskmon {

class ProcessLock {
public:
    ProcessLock() = default;
    ProcessLock(const ProcessLock&) = delete;
    ProcessLock& operator=(const ProcessLock&) = delete;
    ~ProcessLock();

    bool acquire(const std::filesystem::path& pidPath, std::string& error);
    void closeDescriptor();
    int descriptor() const noexcept;

private:
    int lockDescriptor_ = -1;
};

class PidFile {
public:
    explicit PidFile(std::filesystem::path path);
    PidFile(const PidFile&) = delete;
    PidFile& operator=(const PidFile&) = delete;
    ~PidFile();

    bool writeCurrentPid(std::string& error);
    bool removeIfOwned(std::string& error) noexcept;
    const std::filesystem::path& path() const noexcept;

private:
    std::filesystem::path path_;
    dev_t device_ = 0;
    ino_t inode_ = 0;
    pid_t ownerPid_ = -1;
    bool created_ = false;
};

struct DaemonizeResult {
    enum class Role { Parent, Child, Error } role = Role::Error;
    int statusPipe = -1;
    std::string error;
    pid_t launcherChild = -1;
};

DaemonizeResult daemonizeProcess();
bool reportDaemonStartup(int statusPipe, bool success);
bool waitForDaemonStartup(int statusPipe, std::string& error);
bool reapDaemonizationChild(pid_t childPid, std::string& error);
bool processUsesCurrentExecutable(pid_t pid);
} 
