
#pragma once

#include <sys/types.h>

#include <filesystem>
#include <string>

class PidFile {
public:
    enum class ProcState { Missing, Zombie, Foreign, Ours };

    struct Previous {
        enum class Kind { None, Stale, Stopped, Failed };
        Kind kind = Kind::None;
        pid_t pid = 0;
        std::string message;
    };

    explicit PidFile(std::filesystem::path path);
    ~PidFile();

    PidFile(const PidFile&) = delete;
    PidFile& operator=(const PidFile&) = delete;

    const std::filesystem::path& path() const { return path_; }
    bool held() const { return fd_ >= 0; }

    Previous stopPrevious(int timeoutMs = 5000) const;
    bool acquire(std::string& error);
    void release();

    static bool parsePid(const std::string& text, pid_t& pid);
    static std::string stripDeletedSuffix(std::string text);
    static ProcState inspect(pid_t pid);
    static bool lockHeldBy(const std::string& locksText, pid_t pid, dev_t dev, ino_t ino);

private:
    std::filesystem::path path_;
    int fd_ = -1;
    pid_t owner_ = 0;
};
