#include "daemon.hpp"
#include "test_utils.hpp"

#include <gtest/gtest.h>

#include <type_traits>
#include <unistd.h>

using namespace dirclean;
using testutil::listNames;
using testutil::TempDir;
using testutil::writeFile;

static_assert(!std::is_copy_constructible_v<Daemon>, "Daemon must not be copyable");
static_assert(!std::is_copy_assignable_v<Daemon>, "Daemon must not be copy-assignable");
static_assert(!std::is_move_constructible_v<Daemon>, "Daemon must not be movable");
static_assert(!std::is_default_constructible_v<Daemon>, "Daemon constructor must be private");

TEST(DaemonSingleton, ReturnsTheSameInstance) {
    EXPECT_EQ(&Daemon::instance(), &Daemon::instance());
}

TEST(DaemonSingleton, SetConfigPathStoresAbsolutePath) {
    TempDir tmp;
    writeFile(tmp / "c.conf", "interval 4\n");

    char oldCwd[4096];
    ASSERT_NE(getcwd(oldCwd, sizeof oldCwd), nullptr);
    ASSERT_EQ(chdir(tmp.path().c_str()), 0);

    Daemon &d = Daemon::instance();
    bool ok = d.setConfigPath("c.conf");  // относительный путь

    ASSERT_EQ(chdir(oldCwd), 0);
    ASSERT_TRUE(ok);
    EXPECT_EQ(d.configPath(), std::filesystem::canonical(tmp / "c.conf").string());
    EXPECT_TRUE(std::filesystem::path(d.configPath()).is_absolute());
}

TEST(DaemonSingleton, SetConfigPathFailsForMissingFile) {
    TempDir tmp;
    Daemon &d = Daemon::instance();
    ASSERT_TRUE((writeFile(tmp / "c.conf"), d.setConfigPath((tmp / "c.conf").string())));
    std::string before = d.configPath();

    EXPECT_FALSE(d.setConfigPath((tmp / "missing.conf").string()));
    EXPECT_EQ(d.configPath(), before);
}

TEST(DaemonSingleton, ReloadConfigAppliesNewContent) {
    TempDir tmp;
    writeFile(tmp / "c.conf", "interval 4\n/x keep\n");
    Daemon &d = Daemon::instance();
    ASSERT_TRUE(d.setConfigPath((tmp / "c.conf").string()));
    ASSERT_TRUE(d.reloadConfig());
    EXPECT_EQ(d.config().interval, 4u);
    EXPECT_EQ(d.config().rules.size(), 1u);

    writeFile(tmp / "c.conf", "interval 9\n/x keep\n/y keep\n");  
    ASSERT_TRUE(d.reloadConfig());
    EXPECT_EQ(d.config().interval, 9u);
    EXPECT_EQ(d.config().rules.size(), 2u);
}

TEST(DaemonSingleton, FailedReloadKeepsOldConfig) {
    TempDir tmp;
    writeFile(tmp / "c.conf", "interval 6\n/x keep\n");
    Daemon &d = Daemon::instance();
    ASSERT_TRUE(d.setConfigPath((tmp / "c.conf").string()));
    ASSERT_TRUE(d.reloadConfig());

    std::filesystem::remove(tmp / "c.conf");

    EXPECT_FALSE(d.reloadConfig());
    EXPECT_EQ(d.config().interval, 6u);
    EXPECT_EQ(d.config().rules.size(), 1u);
}

TEST(DaemonSingleton, ProcessRulesCleansOnlyUnprotectedFolders) {
    TempDir tmp;
    writeFile(tmp / "open" / "a.txt");
    writeFile(tmp / "open" / "sub" / "b.txt");
    writeFile(tmp / "guarded" / "keep.txt");
    writeFile(tmp / "guarded" / "a.txt");
    writeFile(tmp / "c.conf", "interval 1\nopen keep.txt\nguarded keep.txt\nmissing keep.txt\n");

    Daemon &d = Daemon::instance();
    ASSERT_TRUE(d.setConfigPath((tmp / "c.conf").string()));
    ASSERT_TRUE(d.reloadConfig());

    EXPECT_EQ(d.processRules(), 1u);
    EXPECT_TRUE(listNames(tmp / "open").empty());
    EXPECT_EQ(listNames(tmp / "guarded").size(), 2u);
}

TEST(DaemonSingleton, DefaultPidFileCanBeOverriddenByEnvironment) {
    unsetenv("DIRCLEAN_PID_FILE");
    EXPECT_EQ(Daemon::defaultPidFilePath(), "/tmp/dirclean_daemon.pid");

    setenv("DIRCLEAN_PID_FILE", "/tmp/custom.pid", 1);
    EXPECT_EQ(Daemon::defaultPidFilePath(), "/tmp/custom.pid");
    unsetenv("DIRCLEAN_PID_FILE");
}
