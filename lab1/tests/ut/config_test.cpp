#include "config.hpp"
#include "test_utils.hpp"

#include <gtest/gtest.h>

using testutil::TempDir;
using testutil::writeFile;

class ConfigTest : public ::testing::Test {
protected:
    TempDir tmp;
    std::string conf() const { return tmp.path + "/daemon.conf"; }
};

TEST_F(ConfigTest, ParsesIntervalAndBothDirs) {
    writeFile(conf(), "interval 30\ndir1 /tmp/a\ndir2 /tmp/b\n");
    Config c;
    std::string err;
    ASSERT_TRUE(parseConfig(conf(), c, err)) << err;
    EXPECT_EQ(30u, c.interval);
    EXPECT_EQ("/tmp/a", c.dir1);
    EXPECT_EQ("/tmp/b", c.dir2);
}

TEST_F(ConfigTest, SecondDirIsOptional) {
    writeFile(conf(), "interval 5\ndir1 /tmp/a\n");
    Config c;
    std::string err;
    ASSERT_TRUE(parseConfig(conf(), c, err)) << err;
    EXPECT_TRUE(c.dir2.empty());
}

TEST_F(ConfigTest, IgnoresCommentsAndBlankLines) {
    writeFile(conf(), "# comment\n\ninterval 7   # trailing\n  dir1 /tmp/a  \n");
    Config c;
    std::string err;
    ASSERT_TRUE(parseConfig(conf(), c, err)) << err;
    EXPECT_EQ(7u, c.interval);
    EXPECT_EQ("/tmp/a", c.dir1);
}

TEST_F(ConfigTest, RelativePathIsResolvedAgainstConfigDir) {
    writeFile(conf(), "interval 5\ndir1 sub/dir\n");
    Config c;
    std::string err;
    ASSERT_TRUE(parseConfig(conf(), c, err)) << err;
    EXPECT_EQ(tmp.path + "/sub/dir", c.dir1);
}

TEST_F(ConfigTest, PathMayContainSpaces) {
    writeFile(conf(), "interval 5\ndir1 /tmp/my dir\n");
    Config c;
    std::string err;
    ASSERT_TRUE(parseConfig(conf(), c, err)) << err;
    EXPECT_EQ("/tmp/my dir", c.dir1);
}

TEST_F(ConfigTest, MissingFileFails) {
    Config c;
    std::string err;
    EXPECT_FALSE(parseConfig(tmp.path + "/nope.conf", c, err));
    EXPECT_FALSE(err.empty());
}

TEST_F(ConfigTest, MissingIntervalFails) {
    writeFile(conf(), "dir1 /tmp/a\n");
    Config c;
    std::string err;
    EXPECT_FALSE(parseConfig(conf(), c, err));
}

TEST_F(ConfigTest, MissingDir1Fails) {
    writeFile(conf(), "interval 5\ndir2 /tmp/b\n");
    Config c;
    std::string err;
    EXPECT_FALSE(parseConfig(conf(), c, err));
}

TEST_F(ConfigTest, BadIntervalFails) {
    for (const char* bad : {"0", "-3", "abc", "1.5", "99999999999"}) {
        writeFile(conf(), std::string("interval ") + bad + "\ndir1 /tmp/a\n");
        Config c;
        std::string err;
        EXPECT_FALSE(parseConfig(conf(), c, err)) << "interval " << bad;
    }
}

TEST_F(ConfigTest, UnknownKeyFails) {
    writeFile(conf(), "interval 5\ndir1 /tmp/a\nfoo bar\n");
    Config c;
    std::string err;
    EXPECT_FALSE(parseConfig(conf(), c, err));
    EXPECT_NE(std::string::npos, err.find("foo"));
}

TEST_F(ConfigTest, KeyWithoutValueFails) {
    writeFile(conf(), "interval 5\ndir1\n");
    Config c;
    std::string err;
    EXPECT_FALSE(parseConfig(conf(), c, err));
}

TEST_F(ConfigTest, FailedParseDoesNotTouchOutput) {
    Config c;
    c.interval = 42;
    c.dir1 = "/keep";
    writeFile(conf(), "interval 0\n");
    std::string err;
    EXPECT_FALSE(parseConfig(conf(), c, err));
    EXPECT_EQ(42u, c.interval);
    EXPECT_EQ("/keep", c.dir1);
}

TEST(AbsolutePathTest, MakesRelativePathAbsolute) {
    EXPECT_EQ('/', absolutePath("some/file.conf").front());
}

TEST(AbsolutePathTest, KeepsAbsolutePath) {
    EXPECT_EQ("/etc/x.conf", absolutePath("/etc/x.conf"));
}
