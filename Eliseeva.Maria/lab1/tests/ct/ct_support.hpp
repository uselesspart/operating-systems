#pragma once

#include "test_utils.hpp"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <climits>
#include <condition_variable>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace ct {

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using testutil::TempDir;
using testutil::writeFile;

// Тесты меняют /dev/log и запускают демонов, поэтому выполняются только в контейнере.
inline bool inContainer() { return std::getenv("DIRCLEAN_IN_CONTAINER") != nullptr; }

template <class Pred>
bool waitUntil(Pred pred, std::chrono::milliseconds timeout,
               std::chrono::milliseconds step = 50ms) {
    auto deadline = std::chrono::steady_clock::now() + timeout;
    while (true) {
        if (pred()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(step);
    }
}

// Подменяет /dev/log своим сокетом и собирает всё, что демон пишет в syslog.
class SyslogSink {
public:
    SyslogSink() {
        ::unlink("/dev/log");
        fd_ = ::socket(AF_UNIX, SOCK_DGRAM, 0);
        if (fd_ < 0) throw std::runtime_error("socket() failed");
        sockaddr_un addr;
        std::memset(&addr, 0, sizeof addr);
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, "/dev/log", sizeof addr.sun_path - 1);
        if (::bind(fd_, reinterpret_cast<sockaddr *>(&addr), sizeof addr) != 0) {
            ::close(fd_);
            throw std::runtime_error("cannot bind /dev/log");
        }
        thread_ = std::thread([this] { loop(); });
    }

    ~SyslogSink() {
        stop_ = true;
        thread_.join();
        ::close(fd_);
        ::unlink("/dev/log");
    }

    SyslogSink(const SyslogSink &) = delete;
    SyslogSink &operator=(const SyslogSink &) = delete;

    bool waitFor(const std::string &needle, std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex_);
        return cv_.wait_for(lock, timeout, [&] { return containsLocked(needle); });
    }

    bool contains(const std::string &needle) {
        std::lock_guard<std::mutex> lock(mutex_);
        return containsLocked(needle);
    }

    std::string dump() {
        std::lock_guard<std::mutex> lock(mutex_);
        std::string all;
        for (const auto &m : messages_) all += m + "\n";
        return all;
    }

private:
    void loop() {
        char buf[4096];
        while (!stop_) {
            pollfd pfd = {fd_, POLLIN, 0};
            if (::poll(&pfd, 1, 100) > 0 && (pfd.revents & POLLIN)) {
                ssize_t n = ::recv(fd_, buf, sizeof buf, 0);
                if (n > 0) {
                    std::lock_guard<std::mutex> lock(mutex_);
                    messages_.emplace_back(buf, static_cast<std::size_t>(n));
                    cv_.notify_all();
                }
            }
        }
    }

    bool containsLocked(const std::string &needle) const {
        for (const auto &m : messages_)
            if (m.find(needle) != std::string::npos) return true;
        return false;
    }

    int fd_ = -1;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::mutex mutex_;
    std::condition_variable cv_;
    std::vector<std::string> messages_;
};

// Процесс существует и не является зомби.
inline bool processAlive(long pid) {
    std::string path = "/proc/" + std::to_string(pid) + "/stat";
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char buf[1024] = {0};
    std::size_t n = std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    const char *rp = std::strrchr(buf, ')');
    if (n == 0 || !rp || rp[1] == '\0' || rp[2] == '\0') return false;
    return rp[2] != 'Z' && rp[2] != 'X';
}

inline bool waitGone(long pid, std::chrono::milliseconds timeout) {
    return waitUntil([&] { return !processAlive(pid); }, timeout);
}

struct ProcInfo {
    long ppid = 0, pgrp = 0, session = 0, tty = 0;
};

inline bool readProcInfo(long pid, ProcInfo &info) {
    std::string path = "/proc/" + std::to_string(pid) + "/stat";
    FILE *f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char buf[1024] = {0};
    std::size_t n = std::fread(buf, 1, sizeof buf - 1, f);
    std::fclose(f);
    const char *rp = std::strrchr(buf, ')');
    char state;
    if (n == 0 || !rp) return false;
    return std::sscanf(rp + 2, "%c %ld %ld %ld %ld", &state, &info.ppid, &info.pgrp,
                       &info.session, &info.tty) == 5;
}

inline std::string readLink(const std::string &path) {
    char buf[PATH_MAX];
    ssize_t n = ::readlink(path.c_str(), buf, sizeof buf - 1);
    return n > 0 ? std::string(buf, static_cast<std::size_t>(n)) : std::string();
}

// Запуск: fork + exec, ждём только завершения исходного процесса.
// Возвращает его код выхода.
inline int launchDaemon(const fs::path &cwd, const std::string &configArg,
                        const fs::path &pidFile) {
    pid_t child = fork();
    if (child == 0) {
        if (chdir(cwd.c_str()) != 0) _exit(126);
        setenv("DIRCLEAN_PID_FILE", pidFile.c_str(), 1);
        const char *bin = DAEMON_BIN;
        if (configArg.empty())
            execl(bin, bin, static_cast<char *>(nullptr));
        else
            execl(bin, bin, configArg.c_str(), static_cast<char *>(nullptr));
        _exit(127);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

inline long readPidFile(const fs::path &pidFile) {
    FILE *f = std::fopen(pidFile.c_str(), "r");
    if (!f) return 0;
    long pid = 0;
    if (std::fscanf(f, "%ld", &pid) != 1) pid = 0;
    std::fclose(f);
    return pid;
}

// Ждём, пока демон запишет pid-файл (после демонизации) и процесс будет жив.
inline long waitForPidFile(const fs::path &pidFile, std::chrono::milliseconds timeout,
                           long notEqualTo = 0) {
    long pid = 0;
    waitUntil(
        [&] {
            pid = readPidFile(pidFile);
            return pid > 0 && pid != notEqualTo && processAlive(pid);
        },
        timeout);
    return (pid > 0 && pid != notEqualTo && processAlive(pid)) ? pid : 0;
}

inline void writeConfig(const fs::path &path, const std::string &text) { writeFile(path, text); }

// Базовый класс компонентных тестов.
class DaemonTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!inContainer()) {
            GTEST_SKIP() << "component tests run only in the container (./run_tests.sh)";
        }
        sink_ = std::make_unique<SyslogSink>();
        pidFile_ = tmp_.path() / "daemon.pid";
    }

    void TearDown() override {
        long pid = readPidFile(pidFile_);
        if (pid > 0 && processAlive(pid)) {
            ::kill(static_cast<pid_t>(pid), SIGKILL);
            waitGone(pid, 3s);
        }
        sink_.reset();
    }

    // Запускает демон и возвращает его pid из pid-файла (0 - не запустился).
    long start(const fs::path &cwd, const std::string &configArg, long notEqualTo = 0) {
        int rc = launchDaemon(cwd, configArg, pidFile_);
        EXPECT_EQ(rc, 0) << "launcher exit code";
        return waitForPidFile(pidFile_, 5s, notEqualTo);
    }

    void signalDaemon(long pid, int sig) { ASSERT_EQ(::kill(static_cast<pid_t>(pid), sig), 0); }

    TempDir tmp_;
    fs::path pidFile_;
    std::unique_ptr<SyslogSink> sink_;
};

} 
