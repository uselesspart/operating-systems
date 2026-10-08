#pragma once

#include <chrono>
#include <optional>
#include <string>
#include <sys/types.h>

namespace dirclean {

enum class TerminateStatus {
    NoPrevious,   // pid-файла нет, процесса нет 
    Terminated,   // старый экземпляр получил SIGTERM и завершился
    StillRunning, // SIGTERM отправлен, но процесс не завершился за timeout
    SignalFailed  // не удалось отправить SIGTERM
};

struct TerminateResult {
    TerminateStatus status = TerminateStatus::NoPrevious;
    long pid = 0;
    int errorCode = 0;
};

// Работа с pid-файлом и проверка процессов через /proc.
class PidManager {
public:
    explicit PidManager(std::string pidFile, std::string procRoot = "/proc");
    ~PidManager();

    PidManager(const PidManager &) = delete;
    PidManager &operator=(const PidManager &) = delete;

    const std::string &path() const { return pidFile_; }

    bool acquireStartupLock();
    void releaseStartupLock();

    // pid из файла; nullopt, если файла нет или в нём не положительное число.
    std::optional<long> readPid() const;

    // Процесс существует, если есть /proc/<pid>/stat и он не зомби.
    bool isProcessAlive(long pid) const;

    // Если в pid-файле указан живой процесс - отправить ему SIGTERM
    // и дождаться завершения.
    TerminateResult terminatePrevious(std::chrono::milliseconds timeout) const;

    bool write(pid_t pid) const;

    // Удалить pid-файл, только если он содержит pid (не затираем файл нового экземпляра).
    void removeIfOwner(pid_t pid) const;

private:
    std::string pidFile_;
    std::string procRoot_;
    int lockFd_ = -1;
};

} 
