#pragma once

#include <chrono>
#include <filesystem>
#include <semaphore.h>
#include <signal.h>
#include <string>
#include <sys/types.h>

inline constexpr auto connectionTimeout = std::chrono::seconds(5);
inline constexpr auto idleTimeout = std::chrono::minutes(1);
inline constexpr auto pollInterval = std::chrono::milliseconds(20);
using Clock = std::chrono::steady_clock;

[[noreturn]] void systemError(const std::string& operation);
void prepareSignals();
sigset_t signalSet(int number);
pid_t parsePid(const char* text);
std::string resourceName(pid_t host, pid_t client, const std::string& suffix);
std::filesystem::path hostDirectory(pid_t host);
bool ready(int fd, short events, int timeoutMs = 0);
void waitReady(int fd, short events, Clock::time_point deadline);

class Fd {
public:
    explicit Fd(int value = -1) : value_(value) {}
    ~Fd();
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept;
    Fd& operator=(Fd&& other) noexcept;
    int get() const { return value_; }
    void reset(int value = -1);
private:
    int value_;
};

class Semaphore {
public:
    Semaphore(std::string name, bool create, unsigned value);
    ~Semaphore();
    Semaphore(const Semaphore&) = delete;
    Semaphore& operator=(const Semaphore&) = delete;
    void wait();
    void post();
private:
    std::string name_;
    bool owner_;
    sem_t* value_;
};

class Directory {
public:
    Directory(std::filesystem::path path, bool create);
    ~Directory();
    Directory(const Directory&) = delete;
    Directory& operator=(const Directory&) = delete;
    const std::filesystem::path path;
private:
    bool owner_;
};

class Process {
public:
    explicit Process(pid_t pid);
    bool finished() const;
    void send(int signal) const;
private:
    Fd fd_;
};
