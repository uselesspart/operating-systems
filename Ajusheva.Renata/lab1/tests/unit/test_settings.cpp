#include "check.hpp"

#include "Settings.hpp"

#include <sstream>

#include <sys/stat.h>
#include <unistd.h>

using namespace diskmond;

namespace {


std::string configText(std::initializer_list<std::string> lines) {
    std::string text;
    for (const auto& line : lines) {
        text += line + "\n";
    }
    return text;
}

ParsedSettings parse(const std::string& text, const std::string& baseDir) {
    std::istringstream input(text);
    return parseSettings(input, baseDir, "test.conf");
}

void makeDirectory(const std::string& path) {
    ::mkdir(path.c_str(), 0755);
}

}  

TEST(settings_directives) {
    unit::TempDir dir;
    makeDirectory(dir / "one");
    makeDirectory(dir / "two");
    makeDirectory(dir / "with space");

    const auto loaded = parse(configText({
                                  "# комментарий",
                                  "",
                                  "   \t ",
                                  "watch " + (dir / "one"),
                                  
                                  "WATCH=" + (dir / "two") + "\r",
                                  
                                  "  watch   =   with space  ",
                                  "ignore cache",
                                  "logfile = /var/log/custom.log",
                              }),
                              dir.path());

    CHECK(loaded.remarks.empty());
    CHECK_EQ(loaded.settings.directories, unit::lines({dir / "one", dir / "two", dir / "with space"}));
    CHECK_EQ(loaded.settings.ignored, unit::lines({dir / "cache"}));
    CHECK_EQ(loaded.settings.logFile, "/var/log/custom.log");
}

TEST(settings_defaults) {
    unit::TempDir dir;
    const auto loaded = parse("", dir.path());
    CHECK(loaded.remarks.empty());
    CHECK(loaded.settings.directories.empty());
    CHECK(loaded.settings.ignored.empty());
    CHECK_EQ(loaded.settings.logFile, kDefaultLogFile);
}

TEST(settings_remarks_name_the_line) {
    unit::TempDir dir;
    unit::writeFile(dir / "plain.txt", "");
    const auto loaded = parse(configText({
                                  "watch " + (dir / "missing"),
                                  "watch " + (dir / "plain.txt"),
                                  "frobnicate /x",
                                  "watch",
                              }),
                              dir.path());

    CHECK(loaded.settings.directories.empty());
    CHECK_EQ(loaded.remarks.size(), 4u);
    if (loaded.remarks.size() == 4) {
        CHECK(unit::contains(loaded.remarks[0], "test.conf:1: cannot use"));
        CHECK(unit::contains(loaded.remarks[0], "No such file or directory (line ignored)"));
        CHECK(unit::contains(loaded.remarks[1], "test.conf:2:"));
        CHECK(unit::contains(loaded.remarks[1], "is not a directory (line ignored)"));
        CHECK(unit::contains(loaded.remarks[2], "test.conf:3: unknown keyword 'frobnicate'"));
        CHECK(unit::contains(loaded.remarks[3], "test.conf:4: 'watch' without a path"));
    }
}

TEST(settings_repeated_and_nested_directories) {
    unit::TempDir dir;
    makeDirectory(dir / "a");
    makeDirectory(dir / "a/b");
    makeDirectory(dir / "c");

    const auto loaded = parse(configText({"watch a/b", "watch a", "watch c", "watch ./a/../a"}), dir.path());

    
    CHECK_EQ(loaded.settings.directories, unit::lines({dir / "a", dir / "c"}));

    CHECK_EQ(loaded.remarks.size(), 2u);
    if (loaded.remarks.size() == 2) {
        CHECK(unit::contains(loaded.remarks[0], "test.conf:1:"));
        CHECK(unit::contains(loaded.remarks[0], "is inside '" + (dir / "a") + "'"));
        CHECK(unit::contains(loaded.remarks[1], "test.conf:4:"));
        CHECK(unit::contains(loaded.remarks[1], "is already listed"));
    }
}

TEST(settings_symlink_is_resolved) {
    unit::TempDir dir;
    makeDirectory(dir / "real");
    CHECK(::symlink((dir / "real").c_str(), (dir / "link").c_str()) == 0);
    const auto loaded = parse(configText({"watch link"}), dir.path());
    CHECK_EQ(loaded.settings.directories, unit::lines({dir / "real"}));
}

TEST(settings_utf8_bom_is_skipped) {
    unit::TempDir dir;
    const auto loaded = parse(configText({"\xEF\xBB\xBFwatch " + dir.path()}), dir.path());
    CHECK(loaded.remarks.empty());
    CHECK_EQ(loaded.settings.directories, unit::lines({dir.path()}));
}

TEST(settings_load_from_file) {
    unit::TempDir dir;
    makeDirectory(dir / "conf");
    makeDirectory(dir / "data");
    unit::writeFile(dir / "conf/d.conf", configText({"watch ../data"}));

    std::string error;
    const auto loaded = readSettingsFile(dir / "conf/d.conf", error);
    CHECK(loaded.has_value());
    if (loaded) {
        
        CHECK_EQ(loaded->settings.directories, unit::lines({dir / "data"}));
    }

    CHECK(!readSettingsFile(dir / "missing.conf", error));
    CHECK(unit::contains(error, "cannot be opened: No such file or directory"));
    CHECK(!readSettingsFile(dir.path(), error));
    CHECK(unit::contains(error, "cannot be read: Is a directory"));
}
