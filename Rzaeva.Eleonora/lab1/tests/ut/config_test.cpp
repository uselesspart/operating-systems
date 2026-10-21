#include <gtest/gtest.h>

#include "config.hpp"
#include "test_utils.hpp"

namespace fs = std::filesystem;
using dirclean::parseConfigFile;
using testutil::TempDir;
using testutil::writeFile;

TEST(Config, ParsesIntervalAndRules) {
    TempDir tmp;
    const fs::path folder = tmp.path() / "folder";
    fs::create_directories(folder);
    writeFile(tmp.path() / "daemon.conf", "interval 5\n" + folder.string() + " keep.txt\n");

    const auto result = parseConfigFile(tmp.path() / "daemon.conf");

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.config.intervalSeconds, 5);
    ASSERT_EQ(result.config.rules.size(), 1u);
    EXPECT_EQ(result.config.rules[0].folder, folder);
    EXPECT_EQ(result.config.rules[0].ignFile, folder / "keep.txt");
}

TEST(Config, UsesDefaultIntervalAndWarnsWhenAbsent) {
    TempDir tmp;
    writeFile(tmp.path() / "daemon.conf", (tmp.path() / "f").string() + " g\n");

    const auto result = parseConfigFile(tmp.path() / "daemon.conf");

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.config.intervalSeconds, 20);
    ASSERT_FALSE(result.warnings.empty());
    EXPECT_NE(result.warnings[0].find("interval"), std::string::npos);
}

TEST(Config, ResolvesRelativeFolderAgainstConfigDirectory) {
    TempDir tmp;
    const fs::path confDir = tmp.path() / "cfg";
    fs::create_directories(confDir / "managed");
    writeFile(confDir / "daemon.conf", "interval 1\nmanaged .ignore\n");

    const auto result = parseConfigFile(confDir / "daemon.conf");

    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.config.rules.size(), 1u);
    EXPECT_EQ(result.config.rules[0].folder, confDir / "managed");
    EXPECT_EQ(result.config.rules[0].ignFile, confDir / "managed" / ".ignore");
}

TEST(Config, KeepsAbsoluteFolderAndAbsoluteIgnFile) {
    TempDir tmp;
    const fs::path folder = tmp.path() / "abs";
    const fs::path guard = tmp.path() / "somewhere" / "guard.lock";
    fs::create_directories(folder);
    writeFile(tmp.path() / "daemon.conf",
              "interval 1\n" + folder.string() + " " + guard.string() + "\n");

    const auto result = parseConfigFile(tmp.path() / "daemon.conf");

    ASSERT_TRUE(result.ok) << result.error;
    ASSERT_EQ(result.config.rules.size(), 1u);
    EXPECT_EQ(result.config.rules[0].folder, folder);
    EXPECT_EQ(result.config.rules[0].ignFile, guard);
}

TEST(Config, IgnoresCommentsBlankLinesAndExtraSpaces) {
    TempDir tmp;
    const fs::path folder = tmp.path() / "f";
    fs::create_directories(folder);
    writeFile(tmp.path() / "daemon.conf",
              "# комментарий\n"
              "\n"
              "   \n"
              "interval   2   \n"
              "   " + folder.string() + "    keep.txt   \n"
              "# ещё комментарий\n");

    const auto result = parseConfigFile(tmp.path() / "daemon.conf");

    ASSERT_TRUE(result.ok) << result.error;
    EXPECT_EQ(result.config.intervalSeconds, 2);
    ASSERT_EQ(result.config.rules.size(), 1u);
    EXPECT_EQ(result.config.rules[0].ignFile, folder / "keep.txt");
}

TEST(Config, MalformedRuleLineBecomesWarningAndIsSkipped) {
    TempDir tmp;
    const fs::path good = tmp.path() / "good";
    fs::create_directories(good);
    writeFile(tmp.path() / "daemon.conf",
              "interval 1\n"
              "onlyoneword\n"
              "too many words here\n"
              + good.string() + " keep.txt\n");

    const auto result = parseConfigFile(tmp.path() / "daemon.conf");

    ASSERT_TRUE(result.ok);
    ASSERT_EQ(result.config.rules.size(), 1u);
    EXPECT_EQ(result.config.rules[0].folder, good);
    ASSERT_EQ(result.warnings.size(), 2u);
    EXPECT_NE(result.warnings[0].find("line 2"), std::string::npos);
    EXPECT_NE(result.warnings[1].find("line 3"), std::string::npos);
}

TEST(Config, BadIntervalKeepsDefaultWithWarning) {
    TempDir tmp;
    writeFile(tmp.path() / "daemon.conf", "interval abc\n" + (tmp.path() / "f").string() + " g\n");

    const auto result = parseConfigFile(tmp.path() / "daemon.conf");

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.config.intervalSeconds, 20);
    ASSERT_FALSE(result.warnings.empty());
}

TEST(Config, NonPositiveIntervalKeepsDefault) {
    TempDir tmp;
    writeFile(tmp.path() / "daemon.conf", "interval 0\n" + (tmp.path() / "f").string() + " g\n");

    const auto result = parseConfigFile(tmp.path() / "daemon.conf");

    ASSERT_TRUE(result.ok);
    EXPECT_EQ(result.config.intervalSeconds, 20);
}

TEST(Config, MissingFileReportsError) {
    TempDir tmp;
    const auto result = parseConfigFile(tmp.path() / "nope.conf");

    EXPECT_FALSE(result.ok);
    EXPECT_FALSE(result.error.empty());
}

TEST(Config, EmptyRulesAreAllowed) {
    TempDir tmp;
    writeFile(tmp.path() / "daemon.conf", "interval 3\n");

    const auto result = parseConfigFile(tmp.path() / "daemon.conf");

    ASSERT_TRUE(result.ok);
    EXPECT_TRUE(result.config.rules.empty());
    EXPECT_EQ(result.config.intervalSeconds, 3);
}
