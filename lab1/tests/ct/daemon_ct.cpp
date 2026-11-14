// Компонентные тесты: запускается настоящий бинарник демона (DAEMON_BIN).
#include "pid_manager.hpp"
#include "test_utils.hpp"

#include <csignal>
#include <cstring>
#include <gtest/gtest.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

namespace fs = std::filesystem;
using testutil::waitFor;
using testutil::writeFile;

namespace {

// Подставляет сокет /dev/log и накапливает всё, что демон пишет в syslog.
// Создаётся только если /dev/log отсутствует (чтобы не сломать журнал хоста),
// например в чистом Docker-контейнере; иначе ok() == false и тест пропускается.
class FakeSyslog {
public:
    FakeSyslog() {
        struct stat st;
        if (lstat("/dev/log", &st) == 0) return;
        fd_ = socket(AF_UNIX, SOCK_DGRAM | SOCK_NONBLOCK, 0);
        if (fd_ < 0) return;
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, "/dev/log", sizeof(addr.sun_path) - 1);
        if (bind(fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
            close(fd_);
            fd_ = -1;
            return;
        }
        chmod("/dev/log", 0666);
        bound_ = true;
    }
    ~FakeSyslog() {
        if (fd_ >= 0) close(fd_);
        if (bound_) unlink("/dev/log");
    }
    bool ok() const { return bound_; }
    bool waitForText(const std::string& text, int ms = 5000) {
        return waitFor(
            [&] {
                drain();
                return log_.find(text) != std::string::npos;
            },
            ms);
    }

private:
    void drain() {
        char buf[4096];
        ssize_t n;
        while ((n = recv(fd_, buf, sizeof(buf), 0)) > 0) log_.append(buf, static_cast<size_t>(n));
    }
    int fd_ = -1;
    bool bound_ = false;
    std::string log_;
};

class DaemonCt : public ::testing::Test {
protected:
    testutil::TempDir tmp;
    std::string pidFile, conf, dir1, dir2;

    void SetUp() override {
        pidFile = tmp.path + "/daemon.pid";
        conf = tmp.path + "/daemon.conf";
        dir1 = tmp.path + "/a";
        dir2 = tmp.path + "/b";
        fs::create_directories(dir1);
        fs::create_directories(dir2);
    }

    void TearDown() override {
        pid_t pid = 0;
        if (PidManager(pidFile).readPid(pid) && PidManager::processExists(pid)) {
            kill(pid, SIGKILL);
            waitFor([&] { return !PidManager::processExists(pid); });
        }
    }

    void writeConfig(unsigned interval, bool withDir2 = false) {
        std::string text = "interval " + std::to_string(interval) + "\ndir1 " + dir1 + "\n";
        if (withDir2) text += "dir2 " + dir2 + "\n";
        writeFile(conf, text);
    }

    // Запускает ./daemon; возвращает код выхода стартового процесса.
    int start(const std::string& configArg, const std::string& cwd = "/") {
        const pid_t child = fork();
        if (child == 0) {
            setenv("DIRCLEAN_PID_FILE", pidFile.c_str(), 1);
            if (chdir(cwd.c_str()) != 0) _exit(127);
            execl(DAEMON_BIN, DAEMON_BIN, configArg.c_str(), static_cast<char*>(nullptr));
            _exit(127);
        }
        int status = 0;
        waitpid(child, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    }

    // Ждёт появления pid-файла с живым процессом; возвращает pid (или 0).
    pid_t waitDaemon(pid_t different = 0) {
        pid_t pid = 0;
        waitFor([&] {
            return PidManager(pidFile).readPid(pid) && pid != different &&
                   PidManager::processExists(pid);
        });
        return pid;
    }

    bool waitGone(pid_t pid) {
        return waitFor([&] { return !PidManager::processExists(pid); });
    }
};

}  // namespace

TEST_F(DaemonCt, DaemonizesAndWritesPidFile) {
    writeConfig(100);
    ASSERT_EQ(0, start(conf));
    const pid_t pid = waitDaemon();
    ASSERT_GT(pid, 0);
    EXPECT_NE(getpid(), pid);
    EXPECT_EQ("/", fs::read_symlink("/proc/" + std::to_string(pid) + "/cwd").string());
    EXPECT_EQ("/dev/null", fs::read_symlink("/proc/" + std::to_string(pid) + "/fd/0").string());
    kill(pid, SIGTERM);
    EXPECT_TRUE(waitGone(pid));
}

TEST_F(DaemonCt, RemovesSubdirectoriesOfFolder1ButKeepsFiles) {
    fs::create_directories(dir1 + "/sub1/deep");
    fs::create_directories(dir1 + "/sub2");
    writeFile(dir1 + "/sub1/deep/f.txt", "x");
    writeFile(dir1 + "/keep.txt", "x");
    writeConfig(100);

    ASSERT_EQ(0, start(conf));
    ASSERT_GT(waitDaemon(), 0);
    EXPECT_TRUE(waitFor([&] { return !fs::exists(dir1 + "/sub1") && !fs::exists(dir1 + "/sub2"); }));
    EXPECT_TRUE(fs::exists(dir1 + "/keep.txt"));
    EXPECT_TRUE(fs::exists(dir1));
}

TEST_F(DaemonCt, Folder2IsNotTouched) {
    fs::create_directories(dir1 + "/sub");
    fs::create_directories(dir2 + "/sub");
    writeConfig(100, true);

    ASSERT_EQ(0, start(conf));
    ASSERT_GT(waitDaemon(), 0);
    ASSERT_TRUE(waitFor([&] { return !fs::exists(dir1 + "/sub"); }));
    EXPECT_TRUE(fs::exists(dir2 + "/sub"));
}

TEST_F(DaemonCt, CleansPeriodically) {
    writeConfig(1);
    ASSERT_EQ(0, start(conf));
    ASSERT_GT(waitDaemon(), 0);
    fs::create_directories(dir1 + "/late");
    EXPECT_TRUE(waitFor([&] { return !fs::exists(dir1 + "/late"); }, 6000));
}

TEST_F(DaemonCt, DoesNotCleanBeforeIntervalElapses) {
    writeConfig(100);
    ASSERT_EQ(0, start(conf));
    ASSERT_GT(waitDaemon(), 0);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));  // первая проверка уже была
    fs::create_directories(dir1 + "/late");
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    EXPECT_TRUE(fs::exists(dir1 + "/late"));
}

TEST_F(DaemonCt, SighupRereadsConfigGivenAsRelativePath) {
    writeConfig(100);
    ASSERT_EQ(0, start("daemon.conf", tmp.path));  // относительный путь, потом chdir("/")
    const pid_t pid = waitDaemon();
    ASSERT_GT(pid, 0);

    fs::create_directories(dir2 + "/sub");
    writeFile(conf, "interval 100\ndir1 " + dir2 + "\n");  // теперь чистим папку b
    kill(pid, SIGHUP);

    EXPECT_TRUE(waitFor([&] { return !fs::exists(dir2 + "/sub"); }));
    EXPECT_TRUE(PidManager::processExists(pid));
}

TEST_F(DaemonCt, InvalidConfigOnSighupKeepsOldSettings) {
    writeConfig(1);
    ASSERT_EQ(0, start(conf));
    const pid_t pid = waitDaemon();
    ASSERT_GT(pid, 0);

    writeFile(conf, "это не конфиг\n");
    kill(pid, SIGHUP);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    ASSERT_TRUE(PidManager::processExists(pid));

    fs::create_directories(dir1 + "/late");  // старые настройки (interval 1, dir1) ещё работают
    EXPECT_TRUE(waitFor([&] { return !fs::exists(dir1 + "/late"); }, 6000));
}

TEST_F(DaemonCt, SigtermStopsDaemonAndRemovesPidFile) {
    writeConfig(100);
    ASSERT_EQ(0, start(conf));
    const pid_t pid = waitDaemon();
    ASSERT_GT(pid, 0);
    kill(pid, SIGTERM);
    EXPECT_TRUE(waitGone(pid));
    EXPECT_TRUE(waitFor([&] { return !fs::exists(pidFile); }));
}

TEST_F(DaemonCt, SecondLaunchStopsFirstInstance) {
    writeConfig(100);
    ASSERT_EQ(0, start(conf));
    const pid_t first = waitDaemon();
    ASSERT_GT(first, 0);

    ASSERT_EQ(0, start(conf));
    const pid_t second = waitDaemon(first);
    ASSERT_GT(second, 0);
    EXPECT_NE(first, second);
    EXPECT_TRUE(waitGone(first));
    EXPECT_TRUE(PidManager::processExists(second));
}

TEST_F(DaemonCt, StalePidFileDoesNotPreventStart) {
    writeFile(pidFile, "2147483646\n");
    writeConfig(100);
    ASSERT_EQ(0, start(conf));
    EXPECT_GT(waitDaemon(2147483646), 0);
}

TEST_F(DaemonCt, MissingConfigFailsWithoutPidFile) {
    EXPECT_NE(0, start(tmp.path + "/nope.conf"));
    EXPECT_FALSE(fs::exists(pidFile));
}

TEST_F(DaemonCt, InvalidConfigFailsWithoutPidFile) {
    writeFile(conf, "interval 0\ndir1 " + dir1 + "\n");
    EXPECT_NE(0, start(conf));
    EXPECT_FALSE(fs::exists(pidFile));
}

TEST_F(DaemonCt, WritesLifecycleMessagesToSyslog) {
    FakeSyslog log;
    if (!log.ok()) GTEST_SKIP() << "/dev/log занят или недоступен (запустите в Docker от root)";

    fs::create_directories(dir1 + "/sub");
    writeConfig(100);
    ASSERT_EQ(0, start(conf));
    const pid_t pid = waitDaemon();
    ASSERT_GT(pid, 0);
    EXPECT_TRUE(log.waitForText("dirclean_daemon"));
    EXPECT_TRUE(log.waitForText("daemon started"));
    EXPECT_TRUE(log.waitForText("removed 1 subdirectories"));

    kill(pid, SIGHUP);
    EXPECT_TRUE(log.waitForText("configuration reloaded"));

    kill(pid, SIGTERM);
    EXPECT_TRUE(log.waitForText("received SIGTERM, terminating"));
}

TEST_F(DaemonCt, ReportsConfigErrorsToSyslog) {
    FakeSyslog log;
    if (!log.ok()) GTEST_SKIP() << "/dev/log занят или недоступен (запустите в Docker от root)";

    writeFile(conf, "interval abc\n");
    EXPECT_NE(0, start(conf));
    EXPECT_TRUE(log.waitForText("invalid configuration"));
}
