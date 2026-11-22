#include <gtest/gtest.h>

#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

#include "pid_manager.hpp"
#include "test_utils.hpp"

namespace fs = std::filesystem;
using dirclean::PidManager;
using testutil::TempDir;
using testutil::writeFile;

TEST(PidManager, WriteReadRoundTrip) {
    TempDir tmp;
    const PidManager pm(tmp.path() / "d.pid");

    pm.write(4242);

    EXPECT_EQ(pm.read(), 4242L);
}

TEST(PidManager, WriteOverwrites) {
    TempDir tmp;
    const PidManager pm(tmp.path() / "d.pid");
    pm.write(1);
    pm.write(999);

    EXPECT_EQ(pm.read(), 999L);
}

TEST(PidManager, ReadMissingOrGarbageIsZero) {
    TempDir tmp;
    const PidManager missing(tmp.path() / "absent.pid");
    EXPECT_EQ(missing.read(), 0L);

    writeFile(tmp.path() / "garbage.pid", "not-a-number\n");
    const PidManager garbage(tmp.path() / "garbage.pid");
    EXPECT_EQ(garbage.read(), 0L);

    writeFile(tmp.path() / "negative.pid", "-7\n");
    const PidManager negative(tmp.path() / "negative.pid");
    EXPECT_EQ(negative.read(), 0L);
}

TEST(PidManager, RemoveDeletesFile) {
    TempDir tmp;
    const PidManager pm(tmp.path() / "d.pid");
    pm.write(10);

    pm.remove();

    EXPECT_FALSE(fs::exists(pm.path()));
    pm.remove();
}

TEST(PidManager, ProcessAliveForSelf) {
    EXPECT_TRUE(PidManager::processAlive(static_cast<long>(::getpid())));
}

TEST(PidManager, ProcessAliveRejectsInvalidPids) {
    EXPECT_FALSE(PidManager::processAlive(0));
    EXPECT_FALSE(PidManager::processAlive(-1));
    EXPECT_FALSE(PidManager::processAlive(1L << 30));
}

TEST(PidManager, ProcessAliveFalseForDeadChild) {
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) _exit(0);

    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);

    EXPECT_FALSE(PidManager::processAlive(static_cast<long>(child)));
}

TEST(PidManager, ProcessAliveFalseForZombie) {
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) _exit(0);

    // Родитель ещё не вызвал wait, поэтому процесс в состоянии зомби.
    for (int i = 0; i < 300 && PidManager::processAlive(static_cast<long>(child)); ++i)
        usleep(10 * 1000);
    EXPECT_FALSE(PidManager::processAlive(static_cast<long>(child)));

    int status = 0;
    waitpid(child, &status, 0);
}

TEST(PidManager, StopPreviousTerminatesRunningProcess) {
    TempDir tmp;
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        for (;;) pause();
    }

    const PidManager pm(tmp.path() / "d.pid");
    pm.write(child);

    const long stopped = pm.stopPrevious(3000);
    EXPECT_EQ(stopped, static_cast<long>(child));

    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    EXPECT_TRUE(WIFSIGNALED(status));
    EXPECT_EQ(WTERMSIG(status), SIGTERM);
}

TEST(PidManager, StopPreviousIgnoresStalePid) {
    TempDir tmp;
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) _exit(0);
    int status = 0;
    waitpid(child, &status, 0);

    const PidManager pm(tmp.path() / "d.pid");
    pm.write(child);

    EXPECT_EQ(pm.stopPrevious(500), 0L);
}

TEST(PidManager, StopPreviousWithNoFileIsZero) {
    TempDir tmp;
    const PidManager pm(tmp.path() / "d.pid");

    EXPECT_EQ(pm.stopPrevious(100), 0L);
}
