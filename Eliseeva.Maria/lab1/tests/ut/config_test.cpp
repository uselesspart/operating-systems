#include "config.hpp"
#include "test_utils.hpp"

#include <gtest/gtest.h>

#include <sstream>

using namespace dirclean;
using testutil::TempDir;
using testutil::writeFile;

namespace {

Config parse(const std::string &text, const std::string &base = "/base",
             std::vector<std::string> *warnings = nullptr) {
    std::istringstream in(text);
    return parseConfig(in, base, warnings);
}

} 

TEST(Config, ParsesRulesAndInterval) {
    Config c = parse("interval 5\n/a keep\n/b .ign\n");
    EXPECT_EQ(c.interval, 5u);
    ASSERT_EQ(c.rules.size(), 2u);
    EXPECT_EQ(c.rules[0].folder, "/a");
    EXPECT_EQ(c.rules[0].ignfile, "keep");
    EXPECT_EQ(c.rules[1].folder, "/b");
    EXPECT_EQ(c.rules[1].ignfile, ".ign");
}

TEST(Config, DefaultIntervalWhenMissing) {
    Config c = parse("/a keep\n");
    EXPECT_EQ(c.interval, DEFAULT_INTERVAL);
}

TEST(Config, EmptyConfigGivesNoRules) {
    Config c = parse("");
    EXPECT_TRUE(c.rules.empty());
    EXPECT_EQ(c.interval, DEFAULT_INTERVAL);
}

TEST(Config, IgnoresCommentsAndBlankLines) {
    std::vector<std::string> warnings;
    Config c = parse("# comment\n\n   \n/a keep\n# /b keep\n", "/base", &warnings);
    ASSERT_EQ(c.rules.size(), 1u);
    EXPECT_EQ(c.rules[0].folder, "/a");
    EXPECT_TRUE(warnings.empty());
}

TEST(Config, ArbitraryNumberOfRules) {
    std::string text;
    for (int i = 0; i < 100; ++i) text += "/dir" + std::to_string(i) + " f\n";
    EXPECT_EQ(parse(text).rules.size(), 100u);
}

TEST(Config, ExtraWhitespaceAndTabsAreAllowed) {
    Config c = parse("  /a \t keep  \n\tinterval\t7\n");
    ASSERT_EQ(c.rules.size(), 1u);
    EXPECT_EQ(c.rules[0].folder, "/a");
    EXPECT_EQ(c.rules[0].ignfile, "keep");
    EXPECT_EQ(c.interval, 7u);
}

TEST(Config, RelativeFolderIsResolvedAgainstBaseDir) {
    Config c = parse("data keep\n../other keep\n", "/etc/app");
    ASSERT_EQ(c.rules.size(), 2u);
    EXPECT_EQ(c.rules[0].folder, "/etc/app/data");
    EXPECT_EQ(c.rules[1].folder, "/etc/other");
}

TEST(Config, AbsoluteFolderIsKept) {
    Config c = parse("/var/tmp/x keep\n", "/etc/app");
    ASSERT_EQ(c.rules.size(), 1u);
    EXPECT_EQ(c.rules[0].folder, "/var/tmp/x");
}

TEST(Config, BadIntervalIsIgnoredWithWarning) {
    std::vector<std::string> warnings;
    Config c = parse("interval abc\ninterval -3\ninterval 0\n", "/base", &warnings);
    EXPECT_EQ(c.interval, DEFAULT_INTERVAL);
    ASSERT_EQ(warnings.size(), 3u);
    EXPECT_NE(warnings[0].find("Config line 1"), std::string::npos);
    EXPECT_NE(warnings[2].find("Config line 3"), std::string::npos);
}

TEST(Config, LineWithoutIgnfileIsSkippedWithWarning) {
    std::vector<std::string> warnings;
    Config c = parse("/a keep\n/lonely\n/b keep\n", "/base", &warnings);
    EXPECT_EQ(c.rules.size(), 2u);
    ASSERT_EQ(warnings.size(), 1u);
    EXPECT_NE(warnings[0].find("Config line 2"), std::string::npos);
}

TEST(Config, LoadFileResolvesRelativeToConfigDirectory) {
    TempDir tmp;
    writeFile(tmp / "conf" / "daemon.conf", "interval 3\nwork keep\n");

    Config c;
    ASSERT_TRUE(loadConfigFile((tmp / "conf" / "daemon.conf").string(), c));
    EXPECT_EQ(c.interval, 3u);
    ASSERT_EQ(c.rules.size(), 1u);
    EXPECT_EQ(c.rules[0].folder, (tmp / "conf" / "work").string());
}

TEST(Config, LoadMissingFileFailsAndKeepsOutput) {
    Config c;
    c.interval = 99;
    EXPECT_FALSE(loadConfigFile("/nonexistent/dir/daemon.conf", c));
    EXPECT_EQ(c.interval, 99u);
}
