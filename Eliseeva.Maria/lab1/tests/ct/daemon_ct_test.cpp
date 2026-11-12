// Компонентные тесты: настоящий бинарник демона как чёрный ящик.
// Проверяются демонизация, действие над папками, сигналы, pid-файл и журнал.

#include "ct_support.hpp"

using namespace ct;
using testutil::listNames;

class ComponentDaemon : public DaemonTest {
protected:
    // Рабочая папка с конфигом; folders - строки правил, interval - в секундах.
    fs::path makeWork(const std::string &rules, int interval = 1) {
        fs::path work = tmp_ / "work";
        fs::create_directories(work);
        writeConfig(work / "daemon.conf", "interval " + std::to_string(interval) + "\n" + rules);
        return work;
    }
};

TEST_F(ComponentDaemon, StartsAsProperDaemon) {
    fs::path work = makeWork((tmp_ / "guarded").string() + " keep.txt\n");
    writeFile(tmp_ / "guarded" / "keep.txt");

    long pid = start(work, (work / "daemon.conf").string());
    ASSERT_GT(pid, 0) << sink_->dump();

    ProcInfo info;
    ASSERT_TRUE(readProcInfo(pid, info));
    EXPECT_EQ(info.session, pid) << "daemon must be a session leader (setsid)";
    EXPECT_EQ(info.tty, 0) << "daemon must have no controlling terminal";
    EXPECT_NE(info.ppid, static_cast<long>(getpid())) << "parent process must have exited";

    EXPECT_EQ(readLink("/proc/" + std::to_string(pid) + "/cwd"), "/");
    for (int fd : {0, 1, 2}) {
        EXPECT_EQ(readLink("/proc/" + std::to_string(pid) + "/fd/" + std::to_string(fd)),
                  "/dev/null");
    }

    EXPECT_TRUE(sink_->waitFor("dirclean_daemon", 5s));
    EXPECT_TRUE(sink_->waitFor("Daemon started", 5s)) << sink_->dump();
}

TEST_F(ComponentDaemon, CleansFolderWithoutIgnfileRepeatedly) {
    fs::path target = tmp_ / "target";
    writeFile(target / "a.txt");
    writeFile(target / ".hidden");
    writeFile(target / "sub" / "b.txt");
    fs::path work = makeWork(target.string() + " keep.txt\n");

    ASSERT_GT(start(work, (work / "daemon.conf").string()), 0);

    EXPECT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 5s)) << sink_->dump();
    EXPECT_TRUE(fs::is_directory(target)) << "the folder itself must survive";
    EXPECT_TRUE(sink_->waitFor("Cleaned " + target.string(), 5s)) << sink_->dump();

    // Следующий цикл (интервал 1 с) должен убрать и новые файлы.
    writeFile(target / "late.txt");
    EXPECT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 6s)) << sink_->dump();
}

TEST_F(ComponentDaemon, KeepsFolderWithIgnfile) {
    fs::path target = tmp_ / "target";
    writeFile(target / "keep.txt");
    writeFile(target / "a.txt");
    writeFile(target / "sub" / "b.txt");
    fs::path work = makeWork(target.string() + " keep.txt\n");

    ASSERT_GT(start(work, (work / "daemon.conf").string()), 0);
    ASSERT_TRUE(sink_->waitFor("Config loaded", 5s));
    std::this_thread::sleep_for(3500ms);  // несколько циклов

    EXPECT_EQ(listNames(target), (std::vector<std::string>{"a.txt", "keep.txt", "sub"}));
    EXPECT_FALSE(sink_->contains("Cleaned " + target.string()));
}

TEST_F(ComponentDaemon, HandlesSeveralFoldersIndependently) {
    fs::path a = tmp_ / "a", b = tmp_ / "b", c = tmp_ / "c";
    writeFile(a / "x");
    writeFile(b / "x");
    writeFile(b / ".ignore");
    writeFile(c / "x");
    fs::path work = makeWork(a.string() + " .ignore\n" + b.string() + " .ignore\n" + c.string() +
                             " other.flag\n");

    ASSERT_GT(start(work, (work / "daemon.conf").string()), 0);

    EXPECT_TRUE(waitUntil([&] { return listNames(a).empty() && listNames(c).empty(); }, 5s));
    EXPECT_EQ(listNames(b).size(), 2u);
}

TEST_F(ComponentDaemon, LogsWarningForBadConfigLinesAndMissingFolders) {
    fs::path work = makeWork("lonely\n" + (tmp_ / "does_not_exist").string() + " keep.txt\n");

    ASSERT_GT(start(work, (work / "daemon.conf").string()), 0);

    EXPECT_TRUE(sink_->waitFor("Config line 2", 5s)) << sink_->dump();
    EXPECT_TRUE(sink_->waitFor("is not a directory", 5s)) << sink_->dump();
}

TEST_F(ComponentDaemon, SighupRereadsConfig) {
    fs::path guarded = tmp_ / "guarded", later = tmp_ / "later";
    writeFile(guarded / "keep.txt");
    writeFile(later / "a.txt");
    fs::path work = makeWork(guarded.string() + " keep.txt\n");
    fs::path cfg = work / "daemon.conf";

    long pid = start(work, cfg.string());
    ASSERT_GT(pid, 0);
    ASSERT_TRUE(sink_->waitFor("Config loaded: 1 rule", 5s));
    std::this_thread::sleep_for(1500ms);
    EXPECT_EQ(listNames(later).size(), 1u) << "folder is not in the config yet";

    writeConfig(cfg, "interval 1\n" + guarded.string() + " keep.txt\n" + later.string() +
                         " keep.txt\n");
    signalDaemon(pid, SIGHUP);

    EXPECT_TRUE(sink_->waitFor("SIGHUP received", 5s)) << sink_->dump();
    EXPECT_TRUE(sink_->waitFor("Config loaded: 2 rule", 5s)) << sink_->dump();
    EXPECT_TRUE(waitUntil([&] { return listNames(later).empty(); }, 5s));
    EXPECT_TRUE(processAlive(pid)) << "SIGHUP must not stop the daemon";
}

TEST_F(ComponentDaemon, SighupAppliesNewInterval) {
    fs::path target = tmp_ / "target";
    writeFile(target / "a.txt");
    fs::path work = makeWork(target.string() + " keep.txt\n", /*interval=*/3600);
    fs::path cfg = work / "daemon.conf";

    long pid = start(work, cfg.string());
    ASSERT_GT(pid, 0);
    ASSERT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 5s));  // первый проход

    writeFile(target / "b.txt");
    std::this_thread::sleep_for(1500ms);
    EXPECT_EQ(listNames(target).size(), 1u) << "interval is 1 hour, nothing should happen";

    writeConfig(cfg, "interval 1\n" + target.string() + " keep.txt\n");
    signalDaemon(pid, SIGHUP);
    EXPECT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 5s)) << sink_->dump();
}

TEST_F(ComponentDaemon, SighupWithBrokenConfigKeepsOldRules) {
    fs::path target = tmp_ / "target";
    writeFile(target / "a.txt");
    fs::path work = makeWork(target.string() + " keep.txt\n");
    fs::path cfg = work / "daemon.conf";

    long pid = start(work, cfg.string());
    ASSERT_GT(pid, 0);
    ASSERT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 5s));

    fs::remove(cfg);
    signalDaemon(pid, SIGHUP);
    EXPECT_TRUE(sink_->waitFor("Reload failed", 5s)) << sink_->dump();

    writeFile(target / "new.txt");
    EXPECT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 6s))
        << "old rules must keep working";
    EXPECT_TRUE(processAlive(pid));
}

TEST_F(ComponentDaemon, RemembersAbsoluteConfigPathAfterStartingFromRelativeOne) {
    // Запуск с относительным путём, затем демон делает chdir("/") - SIGHUP всё равно
    // должен найти конфиг. Относительные папки в конфиге считаются от папки конфига.
    fs::path work = tmp_ / "work";
    writeFile(work / "guarded" / "keep.txt");
    writeFile(work / "managed" / "a.txt");
    writeConfig(work / "daemon.conf", "interval 1\nguarded keep.txt\n");

    long pid = start(work, "daemon.conf");  // относительный путь
    ASSERT_GT(pid, 0) << sink_->dump();
    ASSERT_TRUE(sink_->waitFor("Config loaded: 1 rule", 5s));
    EXPECT_NE(sink_->dump().find((work / "daemon.conf").string()), std::string::npos)
        << "log must contain the absolute config path";

    writeConfig(work / "daemon.conf", "interval 1\nguarded keep.txt\nmanaged keep.txt\n");
    signalDaemon(pid, SIGHUP);

    EXPECT_TRUE(waitUntil([&] { return listNames(work / "managed").empty(); }, 5s))
        << sink_->dump();
    EXPECT_EQ(listNames(work / "guarded").size(), 1u);
}

TEST_F(ComponentDaemon, SigtermLogsAndExitsAndRemovesPidFile) {
    fs::path work = makeWork((tmp_ / "guarded").string() + " keep.txt\n");
    writeFile(tmp_ / "guarded" / "keep.txt");

    long pid = start(work, (work / "daemon.conf").string());
    ASSERT_GT(pid, 0);

    signalDaemon(pid, SIGTERM);

    EXPECT_TRUE(waitGone(pid, 5s)) << "daemon must exit on SIGTERM";
    EXPECT_TRUE(sink_->waitFor("SIGTERM received", 5s)) << sink_->dump();
    EXPECT_FALSE(fs::exists(pidFile_)) << "pid file must be removed";
}

TEST_F(ComponentDaemon, SecondInstanceTerminatesFirstAndTakesOverPidFile) {
    fs::path work = makeWork((tmp_ / "guarded").string() + " keep.txt\n");
    writeFile(tmp_ / "guarded" / "keep.txt");
    std::string cfg = (work / "daemon.conf").string();

    long first = start(work, cfg);
    ASSERT_GT(first, 0);

    long second = start(work, cfg, /*notEqualTo=*/first);
    ASSERT_GT(second, 0) << sink_->dump();

    EXPECT_NE(second, first);
    EXPECT_FALSE(processAlive(first)) << "first instance must be terminated";
    EXPECT_TRUE(processAlive(second));
    EXPECT_EQ(readPidFile(pidFile_), second);
    EXPECT_TRUE(sink_->waitFor("Found running instance", 5s)) << sink_->dump();
}

TEST_F(ComponentDaemon, StalePidFileDoesNotPreventStart) {
    fs::path work = makeWork((tmp_ / "guarded").string() + " keep.txt\n");
    writeFile(tmp_ / "guarded" / "keep.txt");

    // pid уже завершившегося процесса
    pid_t dead = fork();
    if (dead == 0) _exit(0);
    waitpid(dead, nullptr, 0);
    writeFile(pidFile_, std::to_string(dead) + "\n");

    long pid = start(work, (work / "daemon.conf").string());
    ASSERT_GT(pid, 0) << sink_->dump();
    EXPECT_NE(pid, static_cast<long>(dead));
}

TEST_F(ComponentDaemon, MissingConfigFailsWithoutDaemonizing) {
    int rc = launchDaemon(tmp_.path(), (tmp_ / "missing.conf").string(), pidFile_);

    EXPECT_NE(rc, 0);
    EXPECT_FALSE(fs::exists(pidFile_));
    EXPECT_TRUE(sink_->waitFor("not found", 5s)) << sink_->dump();
}

TEST_F(ComponentDaemon, UsesDefaultConfigNameInWorkingDirectory) {
    fs::path target = tmp_ / "target";
    writeFile(target / "a.txt");
    fs::path work = makeWork(target.string() + " keep.txt\n");  // work/daemon.conf

    long pid = start(work, "");  // без аргумента
    ASSERT_GT(pid, 0) << sink_->dump();
    EXPECT_TRUE(waitUntil([&] { return listNames(target).empty(); }, 5s));
}
