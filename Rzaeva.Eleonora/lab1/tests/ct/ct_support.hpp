#pragma once

// Компонентные тесты: запуск бинарника демона и проверка его поведения.

#include "test_utils.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#include <csignal>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef DAEMON_BIN
#error "DAEMON_BIN must be defined (см. tests/CMakeLists.txt)"
#endif

namespace ct {

namespace fs = std::filesystem;
using namespace std::chrono_literals;
using testutil::TempDir;
using testutil::writeFile;

constexpr const char* kIdent = "rzaeva_dir_cleaner";

template <class Pred>
bool waitUntil(Pred pred, std::chrono::milliseconds timeout,
               std::chrono::milliseconds step = 50ms) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    for (;;) {
        if (pred()) return true;
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(step);
    }
}

inline std::string runShell(const std::string& command) {
    std::string output;
    FILE* pipe = ::popen(command.c_str(), "r");
    if (pipe == nullptr) return output;
    char buffer[4096];
    std::size_t n = 0;
    while ((n = std::fread(buffer, 1, sizeof buffer, pipe)) > 0) output.append(buffer, n);
    ::pclose(pipe);
    return output;
}

inline bool journalAvailable() {
    static const bool available = [] {
        FILE* pipe = ::popen(
            "command -v journalctl >/dev/null 2>&1 && "
            "journalctl -n 1 -o cat --no-pager >/dev/null 2>&1",
            "r");
        if (pipe == nullptr) return false;
        return ::pclose(pipe) == 0;
    }();
    return available;
}

inline std::string journalForPid(long pid) {
    return runShell("journalctl _PID=" + std::to_string(pid) +
                    " -o cat --no-pager 2>/dev/null");
}

// Сообщения до демонизации ищутся по ident.
inline std::string journalForIdent(const std::string& ident) {
    return runShell("journalctl -t " + ident + " -o cat --no-pager -n 3000 2>/dev/null");
}

inline bool readPidFile(const fs::path& pidFile, long& pid) {
    FILE* f = std::fopen(pidFile.c_str(), "r");
    if (f == nullptr) return false;
    long value = 0;
    const bool ok = std::fscanf(f, "%ld", &value) == 1;
    std::fclose(f);
    pid = ok ? value : 0;
    return ok && value > 0;
}

inline long readPidFile(const fs::path& pidFile) {
    long pid = 0;
    readPidFile(pidFile, pid);
    return pid;
}

inline bool processAlive(long pid) {
    if (pid <= 0) return false;
    const std::string statPath = "/proc/" + std::to_string(pid) + "/stat";
    FILE* f = std::fopen(statPath.c_str(), "r");
    if (f == nullptr) return false;
    char buffer[1024] = {0};
    const std::size_t n = std::fread(buffer, 1, sizeof buffer - 1, f);
    std::fclose(f);
    const char* rp = std::strrchr(buffer, ')');
    if (n == 0 || rp == nullptr || rp[1] == '\0' || rp[2] == '\0') return false;
    return rp[2] != 'Z' && rp[2] != 'X';
}

inline bool waitGone(long pid, std::chrono::milliseconds timeout) {
    return waitUntil([pid] { return !processAlive(pid); }, timeout);
}

struct ProcInfo {
    long ppid = 0, pgrp = 0, session = 0, tty = 0;
};

inline bool readProcInfo(long pid, ProcInfo& info) {
    const std::string statPath = "/proc/" + std::to_string(pid) + "/stat";
    FILE* f = std::fopen(statPath.c_str(), "r");
    if (f == nullptr) return false;
    char buffer[1024] = {0};
    const std::size_t n = std::fread(buffer, 1, sizeof buffer - 1, f);
    std::fclose(f);
    const char* rp = std::strrchr(buffer, ')');
    if (n == 0 || rp == nullptr) return false;
    char state = 0;
    return std::sscanf(rp + 2, "%c %ld %ld %ld %ld", &state, &info.ppid, &info.pgrp,
                       &info.session, &info.tty) == 5;
}

inline std::string readLink(const std::string& path) {
    char buffer[PATH_MAX];
    const ssize_t n = ::readlink(path.c_str(), buffer, sizeof buffer - 1);
    return n > 0 ? std::string(buffer, static_cast<std::size_t>(n)) : std::string();
}

// Код выхода родительского процесса при демонизации.
inline int launchDaemon(const fs::path& cwd, const std::string& configArg,
                        const fs::path& pidFile) {
    const pid_t child = fork();
    if (child == 0) {
        if (chdir(cwd.c_str()) != 0) _exit(126);
        setenv("RZAEVA_PID_FILE", pidFile.c_str(), 1);
        const char* bin = DAEMON_BIN;
        if (configArg.empty())
            execl(bin, bin, static_cast<char*>(nullptr));
        else
            execl(bin, bin, configArg.c_str(), static_cast<char*>(nullptr));
        _exit(127);
    }
    int status = 0;
    waitpid(child, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

inline long waitForPidFile(const fs::path& pidFile, std::chrono::milliseconds timeout,
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

class DaemonTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!journalAvailable()) {
            GTEST_SKIP() << "journalctl недоступен, компонентные тесты пропущены";
        }
        pidFile_ = tmp_.path() / "daemon.pid";
    }

    void TearDown() override {
        const long pid = readPidFile(pidFile_);
        if (pid > 0 && processAlive(pid)) {
            ::kill(static_cast<pid_t>(pid), SIGKILL);
            waitGone(pid, 3s);
        }
    }

    fs::path makeWork(const std::string& rules, int interval = 1) {
        const fs::path work = tmp_.path() / "work";
        fs::create_directories(work);
        writeFile(work / "daemon.conf", "interval " + std::to_string(interval) + "\n" + rules);
        return work;
    }

    long start(const fs::path& cwd, const std::string& configArg, long notEqualTo = 0) {
        const int rc = launchDaemon(cwd, configArg, pidFile_);
        EXPECT_EQ(rc, 0) << "код выхода процесса-родителя";
        return waitForPidFile(pidFile_, 5s, notEqualTo);
    }

    void signalDaemon(long pid, int sig) {
        ASSERT_EQ(::kill(static_cast<pid_t>(pid), sig), 0);
    }

    bool waitForLog(long pid, const std::string& needle, std::chrono::milliseconds timeout) {
        return waitUntil([&] { return journalForPid(pid).find(needle) != std::string::npos; },
                         timeout, 100ms);
    }

    TempDir tmp_;
    fs::path pidFile_;
};

}
