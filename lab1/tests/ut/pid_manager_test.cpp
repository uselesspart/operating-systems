#include "pid_manager.hpp"
#include "test_utils.hpp"

#include <csignal>
#include <gtest/gtest.h>
#include <sys/wait.h>
#include <unistd.h>

using testutil::TempDir;
using testutil::writeFile;

class PidManagerTest : public ::testing::Test {
protected:
    TempDir tmp;
    PidManager pm{tmp.path + "/test.pid"};
};

TEST_F(PidManagerTest, WriteAndReadRoundTrip) {
    ASSERT_TRUE(pm.writePid(12345));
    pid_t pid = 0;
    ASSERT_TRUE(pm.readPid(pid));
    EXPECT_EQ(12345, pid);
}

TEST_F(PidManagerTest, ReadFailsWithoutFile) {
    pid_t pid = 0;
    EXPECT_FALSE(pm.readPid(pid));
}

TEST_F(PidManagerTest, ReadFailsOnGarbage) {
    writeFile(pm.path(), "not a number\n");
    pid_t pid = 0;
    EXPECT_FALSE(pm.readPid(pid));
}

TEST_F(PidManagerTest, ProcessExistsForSelfAndNotForBogusPid) {
    EXPECT_TRUE(PidManager::processExists(getpid()));
    EXPECT_FALSE(PidManager::processExists(0));
    EXPECT_FALSE(PidManager::processExists(-5));
    EXPECT_FALSE(PidManager::processExists(2147483646));
}

TEST_F(PidManagerTest, RemoveIfOwnedByRemovesOnlyOwnFile) {
    pm.writePid(111);
    pm.removeIfOwnedBy(222);
    pid_t pid = 0;
    EXPECT_TRUE(pm.readPid(pid));
    pm.removeIfOwnedBy(111);
    EXPECT_FALSE(pm.readPid(pid));
}

TEST_F(PidManagerTest, StopOldInstanceWithoutPidFileSucceeds) {
    EXPECT_TRUE(pm.stopOldInstance(1));
}

TEST_F(PidManagerTest, StopOldInstanceWithStalePidSucceeds) {
    pm.writePid(2147483646);
    EXPECT_TRUE(pm.stopOldInstance(1));
}

TEST_F(PidManagerTest, StopOldInstanceTerminatesRunningProcess) {
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        for (;;) pause();  // ждёт SIGTERM (действие по умолчанию — завершение)
    }
    ASSERT_TRUE(pm.writePid(child));
    ASSERT_TRUE(PidManager::processExists(child));

    EXPECT_TRUE(pm.stopOldInstance(5));
    EXPECT_FALSE(PidManager::processExists(child));
    waitpid(child, nullptr, 0);
}

TEST_F(PidManagerTest, StopOldInstanceFailsIfProcessIgnoresSigterm) {
    int ready[2];
    ASSERT_EQ(0, pipe(ready));
    const pid_t child = fork();
    ASSERT_GE(child, 0);
    if (child == 0) {
        signal(SIGTERM, SIG_IGN);
        close(ready[0]);
        (void)!write(ready[1], "x", 1);
        for (;;) pause();
    }
    close(ready[1]);
    char c;
    ASSERT_EQ(1, read(ready[0], &c, 1));
    close(ready[0]);
    ASSERT_TRUE(pm.writePid(child));

    EXPECT_FALSE(pm.stopOldInstance(1));

    kill(child, SIGKILL);
    waitpid(child, nullptr, 0);
}
