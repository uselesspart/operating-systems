#include "Daemon.h"
#include "check.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <type_traits>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

static_assert(!std::is_copy_constructible<Daemon>::value, "Daemon must not be copyable");
static_assert(!std::is_copy_assignable<Daemon>::value, "Daemon must not be assignable");
static_assert(!std::is_default_constructible<Daemon>::value, "Daemon constructor must be private");

namespace {

using Outcome = SafeCleaner::Outcome;
using Level = Logger::Level;

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

bool present(const fs::path& path) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(path, ec));
}

std::string slurp(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

SafeCleaner::Report makeReport(Outcome outcome, std::size_t files, std::size_t dirs,
                               std::vector<std::string> messages = {}) {
    SafeCleaner::Report report;
    report.folder = "/data/a";
    report.outcome = outcome;
    report.removedFiles = files;
    report.removedDirs = dirs;
    report.messages = std::move(messages);
    return report;
}

void testSingleton() {
    Daemon& first = Daemon::instance();
    Daemon& second = Daemon::instance();
    CHECK(&first == &second);
}

void testParseArgumentsValid() {
    Daemon::Options options;
    std::string error;
    CHECK(Daemon::parseArguments({}, options, error));
    CHECK(options.configPath == fs::path("config.conf"));
    CHECK(options.pidPath == fs::path("/tmp/daemon_lab1.pid"));
    CHECK(!options.foreground);
    CHECK(!options.help);

    CHECK(Daemon::parseArguments(
        std::vector<std::string>{"--foreground", "--pid-file", "/x/a.pid", "my.conf"}, options,
        error));
    CHECK(options.foreground);
    CHECK(options.pidPath == fs::path("/x/a.pid"));
    CHECK(options.configPath == fs::path("my.conf"));

    CHECK(Daemon::parseArguments(std::vector<std::string>{"-f", "--pid-file=/y.pid"}, options,
                                 error));
    CHECK(options.foreground);
    CHECK(options.pidPath == fs::path("/y.pid"));
    CHECK(options.configPath == fs::path("config.conf"));

    CHECK(Daemon::parseArguments(std::vector<std::string>{"-h"}, options, error));
    CHECK(options.help);
    CHECK(Daemon::parseArguments(std::vector<std::string>{"--help"}, options, error));
    CHECK(options.help);
}

void testParseArgumentsInvalid() {
    const std::vector<std::vector<std::string>> bad = {
        {"--bogus"},
        {"-x"},
        {"--pid-file"},
        {"--pid-file", "--foreground"},
        {"--pid-file="},
        {"a.conf", "b.conf"},
        {""},
    };
    for (const std::vector<std::string>& args : bad) {
        Daemon::Options options;
        options.configPath = "keep.conf";
        std::string error;
        CHECK(!Daemon::parseArguments(args, options, error));
        CHECK(!error.empty());
        CHECK(options.configPath == fs::path("keep.conf"));
    }

    Daemon::Options options;
    std::string error;
    CHECK(!Daemon::parseArguments(std::vector<std::string>{"--bogus"}, options, error));
    CHECK(contains(error, "unknown option"));
    CHECK(!Daemon::parseArguments(std::vector<std::string>{"a", "b"}, options, error));
    CHECK(contains(error, "more than one"));
    CHECK(contains(Daemon::usage("prog"), "Usage: prog"));
}

void testResolvePath() {
    TempDir tmp;
    tmp.mkdir("real");
    fs::create_directory_symlink(tmp.path() / "real", tmp.path() / "link");
    tmp.write("real/target.conf");
    fs::create_symlink("target.conf", tmp.path() / "real" / "alias.conf");
    std::string error;

    fs::path resolved = Daemon::resolvePath(tmp.path() / "real" / "a.conf", error);
    CHECK(resolved == tmp.path() / "real" / "a.conf");

    resolved = Daemon::resolvePath(tmp.path() / "link" / "a.conf", error);
    CHECK(resolved == tmp.path() / "real" / "a.conf");

    resolved = Daemon::resolvePath(tmp.path() / "real" / ".." / "real" / "a.conf", error);
    CHECK(resolved == tmp.path() / "real" / "a.conf");

    resolved = Daemon::resolvePath(tmp.path() / "real" / "alias.conf", error);
    CHECK(resolved == tmp.path() / "real" / "alias.conf");

    resolved = Daemon::resolvePath(tmp.path() / "missing_dir" / "x.pid", error);
    CHECK(resolved == tmp.path() / "missing_dir" / "x.pid");

    const fs::path oldCwd = fs::current_path();
    fs::current_path(tmp.path() / "real");
    resolved = Daemon::resolvePath("rel.conf", error);
    fs::current_path(oldCwd);
    CHECK(resolved.is_absolute());
    CHECK(resolved == tmp.path() / "real" / "rel.conf");

    for (const char* bad : {"", "/", "..", "x/..", "x/."}) {
        error.clear();
        CHECK(Daemon::resolvePath(bad, error).empty());
        CHECK(!error.empty());
    }
}

void testDescribe() {
    std::vector<Daemon::LogLine> lines = Daemon::describe(makeReport(Outcome::Cleaned, 0, 0));
    CHECK(lines.empty());

    lines = Daemon::describe(makeReport(Outcome::Cleaned, 2, 1));
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].level == Level::Info);
    CHECK(contains(lines[0].text, "folder '/data/a'"));
    CHECK(contains(lines[0].text, "removed 2 files, 1 directories"));

    lines = Daemon::describe(makeReport(Outcome::Protected, 0, 0));
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].level == Level::Info);
    CHECK(contains(lines[0].text, "protective file present, nothing removed"));

    lines = Daemon::describe(makeReport(
        Outcome::Interrupted, 3, 1,
        {"protective file 'k' appeared during cleanup",
         "cleanup stopped after removing 3 files and 1 directories"}));
    REQUIRE(lines.size() == 3);
    for (const Daemon::LogLine& line : lines) {
        CHECK(line.level == Level::Warning);
    }
    CHECK(contains(lines[0].text, "interrupted after removing 3 files, 1 directories"));
    CHECK(contains(lines[1].text, "appeared during cleanup"));

    lines = Daemon::describe(makeReport(Outcome::Rejected, 0, 0, {"overlaps with '/x'"}));
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].level == Level::Error);
    CHECK(contains(lines[0].text, "rejected, nothing removed"));
    CHECK(contains(lines[1].text, "overlaps with '/x'"));

    lines = Daemon::describe(makeReport(Outcome::Failed, 1, 0, {"cannot remove 'y'"}));
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].level == Level::Error);
    CHECK(contains(lines[0].text, "cleanup failed, removed 1 files, 0 directories"));
    CHECK(contains(lines[1].text, "cannot remove 'y'"));
}

void testRunRefusesInvalidStartup() {
    TempDir tmp;
    tmp.mkdir("ctl");
    tmp.mkdir("work");
    tmp.write("work/data", "x");
    const std::string work = (tmp.path() / "work").string();
    const std::string ctl = (tmp.path() / "ctl").string();
    Daemon& daemon = Daemon::instance();

    Daemon::Options options;
    options.foreground = true;
    options.pidPath = tmp.path() / "ctl" / "d.pid";

    options.configPath = tmp.path() / "ctl" / "missing.conf";
    CHECK_EQ(daemon.run(options), 1);

    options.configPath = tmp.write("ctl/bad.conf", "interval abc\n" + work + " keep\n");
    CHECK_EQ(daemon.run(options), 1);
    CHECK(present(tmp.path() / "work" / "data"));

    options.configPath = tmp.write("ctl/dangerous.conf", "interval 1\n" + ctl + " keep\n");
    CHECK_EQ(daemon.run(options), 1);
    CHECK(present(tmp.path() / "ctl" / "dangerous.conf"));
    CHECK(present(tmp.path() / "ctl" / "bad.conf"));

    const fs::path victim = tmp.write("victim.txt", "keep\n");
    fs::create_symlink(victim, tmp.path() / "ctl" / "link.pid");
    options.configPath = tmp.write("ctl/good.conf", "interval 1\n" + work + " keep\n");
    options.pidPath = tmp.path() / "ctl" / "link.pid";
    CHECK_EQ(daemon.run(options), 1);
    CHECK_EQ(slurp(victim), "keep\n");
    CHECK(present(tmp.path() / "work" / "data"));

    options.pidPath = fs::path();
    CHECK_EQ(daemon.run(options), 1);
    CHECK(present(tmp.path() / "work" / "data"));
}

}

int main() {
    testSingleton();
    testParseArgumentsValid();
    testParseArgumentsInvalid();
    testResolvePath();
    testDescribe();
    testRunRefusesInvalidStartup();
    return check::summary("unit_daemon");
}
