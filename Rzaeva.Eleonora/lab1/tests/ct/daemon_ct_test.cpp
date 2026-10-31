// Компонентные тесты: запуск бинарника демона и проверка его поведения.

#include "ct_support.hpp"

using namespace ct;
using testutil::listNames;

class ComponentDaemon : public DaemonTest {};

TEST_F(ComponentDaemon, StartsAsProperDaemon) {
    const fs::path guarded = tmp_.path() / "guarded";
    writeFile(guarded / "keep.txt", "x");
    const fs::path work = makeWork(guarded.string() + " keep.txt\n");

    const long pid = start(work, (work / "daemon.conf").string());
    ASSERT_GT(pid, 0) << journalForPid(pid);

    ProcInfo info;
    ASSERT_TRUE(readProcInfo(pid, info));
    EXPECT_EQ(info.session, pid);
    EXPECT_EQ(info.tty, 0);
    EXPECT_NE(info.ppid, static_cast<long>(::getpid()));

    EXPECT_EQ(readLink("/proc/" + std::to_string(pid) + "/cwd"), "/");
    for (int fd : {0, 1, 2}) {
        EXPECT_EQ(readLink("/proc/" + std::to_string(pid) + "/fd/" + std::to_string(fd)),
                  "/dev/null");
    }

    EXPECT_TRUE(waitForLog(pid, "Daemon started", 5s)) << journalForPid(pid);
}

TEST_F(ComponentDaemon, CleansFolderWithoutIgnfileRepeatedly) {
    const fs::path target = tmp_.path() / "target";
    writeFile(target / "a.txt", "a");
    writeFile(target / ".hidden", "h");
    writeFile(target / "sub" / "b.txt", "b");
    const fs::path work = makeWork(target.string() + " keep.txt\n");

    const long pid = start(work, (work / "daemon.conf").string());
    ASSERT_GT(pid, 0) << journalForPid(pid);

    EXPECT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 5s)) << journalForPid(pid);
    EXPECT_TRUE(fs::is_directory(target));
    EXPECT_TRUE(waitForLog(pid, "Cleaned " + target.string(), 5s)) << journalForPid(pid);

    writeFile(target / "late.txt", "l");
    EXPECT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 6s)) << journalForPid(pid);
}

TEST_F(ComponentDaemon, KeepsFolderWithIgnfile) {
    const fs::path target = tmp_.path() / "target";
    writeFile(target / "keep.txt", "k");
    writeFile(target / "a.txt", "a");
    writeFile(target / "sub" / "b.txt", "b");
    const fs::path work = makeWork(target.string() + " keep.txt\n");

    const long pid = start(work, (work / "daemon.conf").string());
    ASSERT_GT(pid, 0);
    ASSERT_TRUE(waitForLog(pid, "Config loaded", 5s)) << journalForPid(pid);

    std::this_thread::sleep_for(2500ms);

    const std::vector<std::string> expected{"a.txt", "keep.txt", "sub"};
    EXPECT_EQ(listNames(target), expected);
    EXPECT_FALSE(journalForPid(pid).find("Cleaned " + target.string()) != std::string::npos);
}

TEST_F(ComponentDaemon, HandlesSeveralFoldersIndependently) {
    const fs::path a = tmp_.path() / "a";
    const fs::path b = tmp_.path() / "b";
    const fs::path c = tmp_.path() / "c";
    writeFile(a / "x", "x");
    writeFile(b / "x", "x");
    writeFile(b / ".ignore", "i");
    writeFile(c / "x", "x");
    const fs::path work = makeWork(a.string() + " .ignore\n" + b.string() + " .ignore\n" +
                                   c.string() + " other.flag\n");

    const long pid = start(work, (work / "daemon.conf").string());
    ASSERT_GT(pid, 0);

    EXPECT_TRUE(waitUntil([&] { return listNames(a).empty() && listNames(c).empty(); }, 5s))
        << journalForPid(pid);
    EXPECT_EQ(listNames(b).size(), 2u);
}

TEST_F(ComponentDaemon, LogsWarningForBadLinesAndMissingFolders) {
    const fs::path missing = tmp_.path() / "does_not_exist";
    const fs::path work = makeWork("lonely\n" + missing.string() + " keep.txt\n");

    const long pid = start(work, (work / "daemon.conf").string());
    ASSERT_GT(pid, 0);

    EXPECT_TRUE(waitForLog(pid, "line 2", 5s)) << journalForPid(pid);
    EXPECT_TRUE(waitForLog(pid, "is not a directory", 5s)) << journalForPid(pid);
}

TEST_F(ComponentDaemon, SighupRereadsConfig) {
    const fs::path guarded = tmp_.path() / "guarded";
    const fs::path later = tmp_.path() / "later";
    writeFile(guarded / "keep.txt", "k");
    writeFile(later / "a.txt", "a");
    const fs::path work = makeWork(guarded.string() + " keep.txt\n");
    const fs::path cfg = work / "daemon.conf";

    const long pid = start(work, cfg.string());
    ASSERT_GT(pid, 0);
    ASSERT_TRUE(waitForLog(pid, "Config loaded: 1 rule", 5s)) << journalForPid(pid);
    std::this_thread::sleep_for(1500ms);
    EXPECT_EQ(listNames(later).size(), 1u);

    writeFile(cfg, "interval 1\n" + guarded.string() + " keep.txt\n" + later.string() +
                       " keep.txt\n");
    signalDaemon(pid, SIGHUP);

    EXPECT_TRUE(waitForLog(pid, "SIGHUP received", 5s)) << journalForPid(pid);
    EXPECT_TRUE(waitForLog(pid, "Config loaded: 2 rule", 5s)) << journalForPid(pid);
    EXPECT_TRUE(waitUntil([&] { return listNames(later).empty(); }, 5s)) << journalForPid(pid);
    EXPECT_TRUE(processAlive(pid));
}

TEST_F(ComponentDaemon, SighupAppliesNewInterval) {
    const fs::path target = tmp_.path() / "target";
    writeFile(target / "a.txt", "a");
    const fs::path work = makeWork(target.string() + " keep.txt\n", /*interval=*/3600);
    const fs::path cfg = work / "daemon.conf";

    const long pid = start(work, cfg.string());
    ASSERT_GT(pid, 0);
    ASSERT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 5s));

    writeFile(target / "b.txt", "b");
    std::this_thread::sleep_for(1500ms);
    EXPECT_EQ(listNames(target).size(), 1u);

    writeFile(cfg, "interval 1\n" + target.string() + " keep.txt\n");
    signalDaemon(pid, SIGHUP);

    EXPECT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 6s)) << journalForPid(pid);
}

TEST_F(ComponentDaemon, SighupWithBrokenConfigKeepsOldRules) {
    const fs::path target = tmp_.path() / "target";
    writeFile(target / "a.txt", "a");
    const fs::path work = makeWork(target.string() + " keep.txt\n");
    const fs::path cfg = work / "daemon.conf";

    const long pid = start(work, cfg.string());
    ASSERT_GT(pid, 0);
    ASSERT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 5s));

    fs::remove(cfg);
    signalDaemon(pid, SIGHUP);
    EXPECT_TRUE(waitForLog(pid, "Reload failed", 5s)) << journalForPid(pid);

    writeFile(target / "new.txt", "n");
    EXPECT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 6s)) << journalForPid(pid);
    EXPECT_TRUE(processAlive(pid));
}

TEST_F(ComponentDaemon, RemembersAbsoluteConfigPathAfterRelativeStart) {
    const fs::path work = tmp_.path() / "work";
    writeFile(work / "guarded" / "keep.txt", "k");
    writeFile(work / "managed" / "a.txt", "a");
    writeFile(work / "daemon.conf", "interval 1\nguarded keep.txt\n");

    const long pid = start(work, "daemon.conf");
    ASSERT_GT(pid, 0) << journalForPid(pid);
    EXPECT_TRUE(waitForLog(pid, "Config loaded: 1 rule", 5s)) << journalForPid(pid);
    EXPECT_NE(journalForPid(pid).find((work / "daemon.conf").string()), std::string::npos);

    writeFile(work / "daemon.conf", "interval 1\nguarded keep.txt\nmanaged keep.txt\n");
    signalDaemon(pid, SIGHUP);

    EXPECT_TRUE(waitUntil([&] { return listNames(work / "managed").empty(); }, 6s))
        << journalForPid(pid);
    EXPECT_EQ(listNames(work / "guarded").size(), 1u);
}

TEST_F(ComponentDaemon, SigtermLogsExitsAndRemovesPidFile) {
    const fs::path guarded = tmp_.path() / "guarded";
    writeFile(guarded / "keep.txt", "k");
    const fs::path work = makeWork(guarded.string() + " keep.txt\n");

    const long pid = start(work, (work / "daemon.conf").string());
    ASSERT_GT(pid, 0);

    signalDaemon(pid, SIGTERM);

    EXPECT_TRUE(waitGone(pid, 5s));
    EXPECT_TRUE(waitForLog(pid, "SIGTERM received", 5s)) << journalForPid(pid);
    EXPECT_TRUE(waitForLog(pid, "Daemon stopped", 5s)) << journalForPid(pid);
    EXPECT_FALSE(fs::exists(pidFile_));
}

TEST_F(ComponentDaemon, SecondInstanceTerminatesFirstAndTakesOverPidFile) {
    const fs::path guarded = tmp_.path() / "guarded";
    writeFile(guarded / "keep.txt", "k");
    const fs::path work = makeWork(guarded.string() + " keep.txt\n");
    const std::string cfg = (work / "daemon.conf").string();

    const long first = start(work, cfg);
    ASSERT_GT(first, 0);

    const long second = start(work, cfg, /*notEqualTo=*/first);
    ASSERT_GT(second, 0) << journalForPid(second);

    EXPECT_NE(second, first);
    EXPECT_FALSE(processAlive(first));
    EXPECT_TRUE(processAlive(second));
    EXPECT_EQ(readPidFile(pidFile_), second);
    EXPECT_TRUE(waitForLog(second, "Found running instance", 5s)) << journalForPid(second);
}

TEST_F(ComponentDaemon, StalePidFileDoesNotPreventStart) {
    const fs::path guarded = tmp_.path() / "guarded";
    writeFile(guarded / "keep.txt", "k");
    const fs::path work = makeWork(guarded.string() + " keep.txt\n");

    const pid_t dead = fork();
    ASSERT_GE(dead, 0);
    if (dead == 0) _exit(0);
    int status = 0;
    waitpid(dead, &status, 0);
    writeFile(pidFile_, std::to_string(dead) + "\n");

    const long pid = start(work, (work / "daemon.conf").string());
    ASSERT_GT(pid, 0) << journalForPid(pid);
    EXPECT_NE(pid, static_cast<long>(dead));
}

TEST_F(ComponentDaemon, MissingConfigFailsWithoutDaemonizing) {
    const fs::path missing = tmp_.path() / "missing.conf";

    const int rc = launchDaemon(tmp_.path(), missing.string(), pidFile_);

    EXPECT_NE(rc, 0);
    EXPECT_FALSE(fs::exists(pidFile_));
    EXPECT_TRUE(waitUntil(
        [&] { return journalForIdent(kIdent).find(missing.string()) != std::string::npos; }, 5s))
        << journalForIdent(kIdent);
}

TEST_F(ComponentDaemon, UsesDefaultConfigNameInWorkingDirectory) {
    const fs::path target = tmp_.path() / "target";
    writeFile(target / "a.txt", "a");
    const fs::path work = makeWork(target.string() + " keep.txt\n");

    const long pid = start(work, /*configArg=*/"");
    ASSERT_GT(pid, 0) << journalForPid(pid);

    EXPECT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 5s)) << journalForPid(pid);
}
