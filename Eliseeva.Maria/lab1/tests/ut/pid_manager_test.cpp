#include "pid_manager.hpp"
#include "test_utils.hpp"

#include <gtest/gtest.h>

#include <csignal>
#include <poll.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace dirclean;
using namespace std::chrono_literals;
using testutil::TempDir;
using testutil::writeFile;

namespace {

// Дочерний процесс, который сообщает о готовности через pipe и ждёт сигналов.
// ignoreSigterm - игнорировать SIGTERM.
pid_t spawnWaiter(bool ignoreSigterm) {
    int fds[2];
    if (pipe(fds) != 0) return -1;
    pid_t pid = fork();
    if (pid == 0) {
        close(fds[0]);
        if (ignoreSigterm) signal(SIGTERM, SIG_IGN);
        char ready = 1;
        if (write(fds[1], &ready, 1) != 1) _exit(2);
        close(fds[1]);
        for (;;) pause();
    }
    close(fds[1]);
    char buf;
    if (read(fds[0], &buf, 1) != 1) { /* ребёнок не стартовал */ }
    close(fds[0]);
    return pid;
}

// Завершившийся и процесс: его pid больше не существует.
pid_t deadPid() {
    pid_t pid = fork();
    if (pid == 0) _exit(0);
    waitpid(pid, nullptr, 0);
    return pid;
}

} 

TEST(PidManager, WriteThenReadReturnsPid) {
    TempDir tmp;
    PidManager pm((tmp / "d.pid").string());
    ASSERT_TRUE(pm.write(4242));
    EXPECT_EQ(pm.readPid(), std::optional<long>(4242));
}

TEST(PidManager, StartupLockSerializesLaunches) {
    TempDir tmp;
    PidManager first((tmp / "d.pid").string());
    ASSERT_TRUE(first.acquireStartupLock());

    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        close(fds[0]);
        PidManager second((tmp / "d.pid").string());
        if (!second.acquireStartupLock()) _exit(2);
        char ready = 1;
        if (write(fds[1], &ready, 1) != 1) _exit(3);
        _exit(0);
    }

    close(fds[1]);
    struct pollfd waitForLock = {fds[0], POLLIN, 0};
    EXPECT_EQ(poll(&waitForLock, 1, 100), 0);
    first.releaseStartupLock();
    EXPECT_EQ(poll(&waitForLock, 1, 1000), 1);
    char ready = 0;
    EXPECT_EQ(read(fds[0], &ready, 1), 1);
    EXPECT_EQ(ready, 1);
    close(fds[0]);
    int status = 0;
    waitpid(child, &status, 0);
    EXPECT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(PidManager, ReadMissingFileGivesNullopt) {
    TempDir tmp;
    EXPECT_FALSE(PidManager((tmp / "none.pid").string()).readPid().has_value());
}

TEST(PidManager, ReadGarbageGivesNullopt) {
    TempDir tmp;
    PidManager pm((tmp / "d.pid").string());
    for (const char *content : {"", "abc", "-5", "0"}) {
        writeFile(tmp / "d.pid", content);
        EXPECT_FALSE(pm.readPid().has_value()) << "content: '" << content << "'";
    }
}

TEST(PidManager, WriteToUnwritablePathFails) {
    EXPECT_FALSE(PidManager("/nonexistent/dir/d.pid").write(1));
}

TEST(PidManager, CurrentProcessIsAlive) {
    EXPECT_TRUE(PidManager("unused").isProcessAlive(getpid()));
}

TEST(PidManager, NonexistentPidIsNotAlive) {
    EXPECT_FALSE(PidManager("unused").isProcessAlive(deadPid()));
}

TEST(PidManager, ZombieIsNotAlive) {
    pid_t pid = fork();
    if (pid == 0) _exit(0);
    PidManager pm("unused");
    // Ждём, пока ребёнок завершится (станет зомби), не вызывая wait.
    for (int i = 0; i < 100 && pm.isProcessAlive(pid); ++i) std::this_thread::sleep_for(20ms);
    EXPECT_FALSE(pm.isProcessAlive(pid));
    waitpid(pid, nullptr, 0);
}

TEST(PidManager, UsesGivenProcRoot) {
    TempDir tmp;
    writeFile(tmp / "proc" / "123" / "stat", "123 (my (daemon)) S 1 123 123 0 -1 0");
    writeFile(tmp / "proc" / "124" / "stat", "124 (zombie) Z 1 124 124 0 -1 0");
    PidManager pm("unused", (tmp / "proc").string());

    EXPECT_TRUE(pm.isProcessAlive(123));   // скобки в имени процесса не мешают
    EXPECT_FALSE(pm.isProcessAlive(124));  // зомби
    EXPECT_FALSE(pm.isProcessAlive(125));  // нет каталога
}

TEST(PidManager, TerminatePreviousWithoutPidFile) {
    TempDir tmp;
    TerminateResult r = PidManager((tmp / "none.pid").string()).terminatePrevious(100ms);
    EXPECT_EQ(r.status, TerminateStatus::NoPrevious);
}

TEST(PidManager, TerminatePreviousWithStalePid) {
    TempDir tmp;
    PidManager pm((tmp / "d.pid").string());
    pm.write(deadPid());
    EXPECT_EQ(pm.terminatePrevious(100ms).status, TerminateStatus::NoPrevious);
}

TEST(PidManager, TerminatePreviousIgnoresOwnPid) {
    TempDir tmp;
    PidManager pm((tmp / "d.pid").string());
    pm.write(getpid());
    EXPECT_EQ(pm.terminatePrevious(100ms).status, TerminateStatus::NoPrevious);
}

TEST(PidManager, TerminatePreviousSendsSigterm) {
    TempDir tmp;
    pid_t child = spawnWaiter(false);
    ASSERT_GT(child, 0);
    PidManager pm((tmp / "d.pid").string());
    pm.write(child);

    TerminateResult r = pm.terminatePrevious(3s);

    EXPECT_EQ(r.status, TerminateStatus::Terminated);
    EXPECT_EQ(r.pid, static_cast<long>(child));
    int status = 0;
    waitpid(child, &status, 0);
    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGTERM);
}

TEST(PidManager, TerminatePreviousReportsStillRunningWhenSigtermIgnored) {
    TempDir tmp;
    pid_t child = spawnWaiter(true);
    ASSERT_GT(child, 0);
    PidManager pm((tmp / "d.pid").string());
    pm.write(child);

    TerminateResult r = pm.terminatePrevious(300ms);

    EXPECT_EQ(r.status, TerminateStatus::StillRunning);
    EXPECT_EQ(r.pid, static_cast<long>(child));
    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
}

TEST(PidManager, RemoveIfOwnerRemovesOnlyOwnFile) {
    TempDir tmp;
    PidManager pm((tmp / "d.pid").string());

    pm.write(111);
    pm.removeIfOwner(222);  // файл принадлежит другому экземпляру
    EXPECT_TRUE(std::filesystem::exists(tmp / "d.pid"));

    pm.removeIfOwner(111);
    EXPECT_FALSE(std::filesystem::exists(tmp / "d.pid"));
}
