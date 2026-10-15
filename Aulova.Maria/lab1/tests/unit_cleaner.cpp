#include "SafeCleaner.h"
#include "check.h"

#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

using Outcome = SafeCleaner::Outcome;
using Policy = SafeCleaner::Policy;

Config::Rule makeRule(const fs::path& folder, const std::string& ignfile = "keep.me") {
    return Config::Rule{folder, ignfile};
}

bool present(const fs::path& path) {
    std::error_code ec;
    return fs::exists(fs::symlink_status(path, ec));
}

std::size_t countEntries(const fs::path& dir) {
    return static_cast<std::size_t>(
        std::distance(fs::directory_iterator(dir), fs::directory_iterator()));
}

std::size_t countAll(const fs::path& dir) {
    return static_cast<std::size_t>(
        std::distance(fs::recursive_directory_iterator(dir), fs::recursive_directory_iterator()));
}

bool has(const std::vector<fs::path>& list, const fs::path& path) {
    return std::find(list.begin(), list.end(), path) != list.end();
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

bool anyMessage(const SafeCleaner::Report& report, const std::string& part) {
    return std::any_of(report.messages.begin(), report.messages.end(),
                       [&](const std::string& message) { return contains(message, part); });
}

std::string readText(const fs::path& path) {
    std::ifstream in(path);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::vector<fs::path> usableSubdirs(const fs::path& drive, std::size_t limit) {
    std::vector<fs::path> result;
    const std::string prefix = drive.string() + "/";
    std::error_code ec;
    fs::directory_iterator it(drive, ec);
    const fs::directory_iterator end;
    for (; !ec && it != end && result.size() < limit; it.increment(ec)) {
        std::error_code probeEc;
        const fs::path real = fs::canonical(it->path(), probeEc);
        if (!probeEc && fs::is_directory(real, probeEc) && !probeEc &&
            real.string().rfind(prefix, 0) == 0) {
            result.push_back(it->path());
        }
    }
    return result;
}

Policy makePolicy(const fs::path& root) {
    Policy policy;
    policy.protectedTrees = {root / "fake_etc", root / "fake_home" / ".ssh"};
    policy.forbiddenExact = {root / "fake_exact"};
    policy.protectedPaths = {root / "fake_home", root / "app" / "sub" / "daemon.conf",
                             root / "app" / "sub" / "daemon.pid",
                             root / "app" / "sub" / "daemon_bin"};
    return policy;
}

class Fixture {
public:
    TempDir tmp;
    SafeCleaner cleaner;

    Fixture() : cleaner(makePolicy(tmp.path())) {
        tmp.mkdir("fake_etc/inner");
        tmp.write("fake_etc/passwd", "root");
        tmp.write("fake_etc/inner/shadow", "secret");
        tmp.mkdir("fake_home/.ssh");
        tmp.write("fake_home/.ssh/key", "key");
        tmp.write("fake_home/doc.txt", "doc");
        tmp.mkdir("fake_home/work");
        tmp.mkdir("fake_exact/child");
        tmp.write("fake_exact/file", "x");
        tmp.mkdir("app/sub");
        tmp.mkdir("app/other");
        tmp.write("app/sub/daemon.conf");
        tmp.write("app/sub/daemon.pid");
        tmp.write("app/sub/daemon_bin");
        tmp.mkdir("outside");
        tmp.write("outside/precious.txt", "keep");
    }

    fs::path dir(const std::string& relative) const { return tmp.path() / relative; }
};

void testStandardPolicy() {
    TempDir tmp;
    tmp.mkdir("app");
    tmp.mkdir("work");
    const fs::path conf = tmp.write("app/daemon.conf");
    const fs::path pid = tmp.path() / "app" / "daemon.pid";
    const Policy policy = Policy::standard(conf, pid);
    const SafeCleaner cleaner(policy);

    fs::path resolved;
    std::string reason;
    for (const char* dir : {"/", "/etc", "/usr", "/usr/bin", "/bin", "/lib", "/proc", "/sys",
                            "/dev", "/home", "/root", "/var", "/run", "/tmp", "/opt", "/mnt",
                            "/mnt/c"}) {
        CHECK(!cleaner.checkFolder(dir, resolved, reason));
    }

    CHECK(!cleaner.checkFolder("/etc", resolved, reason));
    CHECK(contains(reason, "protected"));

    const char* home = std::getenv("HOME");
    if (home != nullptr && *home != '\0') {
        CHECK(!cleaner.checkFolder(home, resolved, reason));
        CHECK(has(policy.protectedPaths, fs::path(home)));
    }

    CHECK(!cleaner.checkFolder(tmp.path() / "app", resolved, reason));
    CHECK(contains(reason, "contains"));
    CHECK(!cleaner.checkFolder(tmp.path(), resolved, reason));

    CHECK(cleaner.checkFolder(tmp.path() / "work", resolved, reason));
    CHECK(resolved == tmp.path() / "work");

    CHECK(has(policy.protectedPaths, conf));
    CHECK(has(policy.protectedPaths, pid));
    CHECK(has(policy.protectedPaths, fs::read_symlink("/proc/self/exe")));
}

void testStandardPolicyProtectsSymlinkedConfig() {
    TempDir tmp;
    tmp.mkdir("cfgdir");
    tmp.mkdir("linkdir");
    tmp.mkdir("free");
    tmp.write("cfgdir/real.conf");
    fs::create_symlink("../cfgdir/real.conf", tmp.path() / "linkdir" / "link.conf");

    const SafeCleaner cleaner(Policy::standard(tmp.path() / "linkdir" / "link.conf",
                                               tmp.path() / "pid" / "daemon.pid"));
    fs::path resolved;
    std::string reason;
    CHECK(!cleaner.checkFolder(tmp.path() / "cfgdir", resolved, reason));
    CHECK(!cleaner.checkFolder(tmp.path() / "linkdir", resolved, reason));
    CHECK(!cleaner.checkFolder(tmp.path() / "pid", resolved, reason));
    CHECK(cleaner.checkFolder(tmp.path() / "free", resolved, reason));
}

void testWindowsDrivesParsing() {
    const std::string mounts =
        R"(C:\134 /mnt/c 9p rw,noatime,dirsync,aname=drvfs;path=C:\;uid=1000 0 0
D:\134 /win/data\040disk 9p rw,aname=drvfs;path=D:\ 0 0
E:\134 /mnt/e drvfs rw,uid=1000 0 0
/dev/sdc / ext4 rw,relatime 0 0
tools /usr/lib/wsl/drivers 9p rw,aname=drivers;fmask=222 0 0
none /tmp/other tmpfs rw 0 0
broken line
)";
    const std::vector<fs::path> drives = Policy::windowsDrives(mounts);
    REQUIRE(drives.size() == 3);
    CHECK(drives[0] == fs::path("/mnt/c"));
    CHECK(drives[1] == fs::path("/win/data disk"));
    CHECK(drives[2] == fs::path("/mnt/e"));
    CHECK(Policy::windowsDrives("").empty());
}

void testParsedWindowsDriveProtectsWholeTree() {
    TempDir tmp;
    tmp.mkdir("win/c/Users/me/Documents");
    tmp.mkdir("win/c/Program Files");
    tmp.mkdir("win/other");
    tmp.mkdir("work");
    tmp.write("win/c/Users/me/Documents/thesis.txt", "important");

    const std::string mounts = "C:\\134 " + (tmp.path() / "win" / "c").string() +
                               " 9p rw,aname=drvfs;path=C:\\ 0 0\n";
    Policy policy;
    policy.protectedTrees = Policy::windowsDrives(mounts);
    REQUIRE(policy.protectedTrees.size() == 1);
    const SafeCleaner cleaner(policy);

    fs::path resolved;
    std::string reason;
    for (const char* relative : {"win", "win/c", "win/c/Users", "win/c/Users/me/Documents",
                                 "win/c/Program Files"}) {
        CHECK(!cleaner.checkFolder(tmp.path() / relative, resolved, reason));
        CHECK(contains(reason, "protected system directory"));
    }
    CHECK(cleaner.checkFolder(tmp.path() / "win" / "other", resolved, reason));
    CHECK(cleaner.checkFolder(tmp.path() / "work", resolved, reason));

    const SafeCleaner::Report report =
        cleaner.clean(makeRule(tmp.path() / "win" / "c" / "Users" / "me" / "Documents"));
    CHECK(report.outcome == Outcome::Rejected);
    CHECK(present(tmp.path() / "win" / "c" / "Users" / "me" / "Documents" / "thesis.txt"));
}

void testStandardPolicyBlocksWindowsDriveTrees() {
    TempDir tmp;
    const Policy policy = Policy::standard(tmp.path() / "app" / "daemon.conf",
                                           tmp.path() / "app" / "daemon.pid");
    const SafeCleaner cleaner(policy);

    std::vector<fs::path> drives = Policy::windowsDrives(readText("/proc/self/mounts"));
    for (char letter = 'a'; letter <= 'z'; ++letter) {
        const fs::path drive = fs::path("/mnt") / std::string(1, letter);
        CHECK(has(policy.protectedTrees, drive));
        if (!has(drives, drive)) {
            drives.push_back(drive);
        }
    }

    std::size_t checkedSubdirs = 0;
    for (const fs::path& drive : drives) {
        std::error_code ec;
        if (!fs::is_directory(drive, ec)) {
            continue;
        }
        fs::path resolved;
        std::string reason;
        CHECK(!cleaner.checkFolder(drive, resolved, reason));
        CHECK(contains(reason, "protected system directory"));
        for (const fs::path& sub : usableSubdirs(drive, 3)) {
            CHECK(!cleaner.checkFolder(sub, resolved, reason));
            CHECK(contains(reason, "protected system directory"));
            ++checkedSubdirs;
        }
    }
    std::cout << "windows drive subfolders checked: " << checkedSubdirs << '\n';
}

void testCleansEverythingInside() {
    Fixture f;
    const fs::path work = f.dir("work");
    fs::create_directories(work / "sub" / "deep");
    fs::create_directory(work / "empty_dir");
    fs::create_directory(work / ".hidden_dir");
    f.tmp.write("work/a.txt", "a");
    f.tmp.write("work/.hidden", "h");
    f.tmp.write("work/sub/deep/file", "f");
    f.tmp.write("work/readonly.txt", "r");
    fs::permissions(work / "readonly.txt", fs::perms::owner_read);
    f.tmp.write("sibling.txt", "s");

    const SafeCleaner::Report report = f.cleaner.clean(makeRule(work));
    CHECK(report.outcome == Outcome::Cleaned);
    CHECK(report.messages.empty());
    CHECK(fs::is_directory(work));
    CHECK_EQ(countEntries(work), 0u);
    CHECK_EQ(report.removedFiles, 4u);
    CHECK_EQ(report.removedDirs, 4u);
    CHECK_EQ(report.removedTopLevel.size(), 6u);
    CHECK(report.folder == work);
    CHECK(present(f.dir("sibling.txt")));
    CHECK(present(f.dir("outside/precious.txt")));
}

void testIgnfileProtects() {
    for (int kind = 0; kind < 4; ++kind) {
        Fixture f;
        const fs::path work = f.dir("work");
        fs::create_directories(work / "sub");
        f.tmp.write("work/data.txt", "d");
        f.tmp.write("work/sub/x", "x");
        const std::string ign = (kind == 3) ? ".dont_clean" : "keep.me";
        if (kind == 0 || kind == 3) {
            f.tmp.write("work/" + ign);
        } else if (kind == 1) {
            fs::create_directory(work / ign);
        } else {
            fs::create_symlink("no_such_target", work / ign);
        }

        const SafeCleaner::Report report = f.cleaner.clean(makeRule(work, ign));
        CHECK(report.outcome == Outcome::Protected);
        CHECK_EQ(report.removedFiles, 0u);
        CHECK_EQ(report.removedDirs, 0u);
        CHECK(present(work / "data.txt"));
        CHECK(present(work / "sub" / "x"));
        CHECK(present(work / ign));
    }
}

void testIgnfileOnlyCountsInsideFolderItself() {
    Fixture f;
    const fs::path work = f.dir("work");
    fs::create_directories(work / "sub");
    f.tmp.write("work/data.txt", "d");
    f.tmp.write("work/sub/keep.me");
    f.tmp.write("keep.me");

    const SafeCleaner::Report report = f.cleaner.clean(makeRule(work));
    CHECK(report.outcome == Outcome::Cleaned);
    CHECK_EQ(countEntries(work), 0u);
    CHECK(present(f.dir("keep.me")));
}

void testIgnfileAppearsDuringCleanup() {
    Fixture f;
    const fs::path work = f.dir("work");
    fs::create_directories(work / "sub" / "deep");
    f.tmp.write("work/a.txt", "a");
    f.tmp.write("work/b.txt", "b");
    f.tmp.write("work/e.txt", "e");
    f.tmp.write("work/sub/c.txt", "c");
    f.tmp.write("work/sub/deep/d.txt", "d");
    const std::size_t total = countAll(work);
    REQUIRE(total == 7);

    const std::size_t trigger = 3;
    std::size_t calls = 0;
    f.cleaner.setBeforeRemoveHook([&](const fs::path&) {
        if (++calls == trigger) {
            f.tmp.write("work/keep.me");
        }
    });

    const SafeCleaner::Report report = f.cleaner.clean(makeRule(work));
    CHECK(report.outcome == Outcome::Interrupted);
    CHECK_EQ(calls, trigger);
    CHECK_EQ(report.removedFiles + report.removedDirs, trigger - 1);
    CHECK_EQ(countAll(work), total - (trigger - 1) + 1);
    CHECK(present(work / "keep.me"));
    CHECK(anyMessage(report, "appeared during cleanup"));
    REQUIRE(!report.messages.empty());
    CHECK(contains(report.messages.back(), "cleanup stopped after removing"));

    f.cleaner.setBeforeRemoveHook(SafeCleaner::Hook());
    const std::size_t afterInterrupt = countAll(work);
    const SafeCleaner::Report second = f.cleaner.clean(makeRule(work));
    CHECK(second.outcome == Outcome::Protected);
    CHECK_EQ(second.removedFiles + second.removedDirs, 0u);
    CHECK_EQ(countAll(work), afterInterrupt);

    fs::remove(work / "keep.me");
    const SafeCleaner::Report third = f.cleaner.clean(makeRule(work));
    CHECK(third.outcome == Outcome::Cleaned);
    CHECK_EQ(countEntries(work), 0u);
}

void testIgnfileAppearsBeforeFirstRemoval() {
    Fixture f;
    const fs::path work = f.dir("work");
    fs::create_directories(work / "sub");
    f.tmp.write("work/a.txt", "a");
    f.tmp.write("work/sub/b.txt", "b");
    const std::size_t total = countAll(work);

    std::size_t calls = 0;
    f.cleaner.setBeforeRemoveHook([&](const fs::path&) {
        if (++calls == 1) {
            f.tmp.write("work/keep.me");
        }
    });

    const SafeCleaner::Report report = f.cleaner.clean(makeRule(work));
    CHECK(report.outcome == Outcome::Protected);
    CHECK_EQ(calls, 1u);
    CHECK_EQ(report.removedFiles, 0u);
    CHECK_EQ(report.removedDirs, 0u);
    CHECK(report.removedTopLevel.empty());
    CHECK_EQ(countAll(work), total + 1);
    CHECK(anyMessage(report, "appeared during cleanup"));
}

void testDirectoryReplacedDuringCleanup() {
    for (const std::size_t trigger : std::vector<std::size_t>{1, 2}) {
        Fixture f;
        const fs::path work = f.dir("work");
        fs::create_directories(work / "sub");
        f.tmp.write("work/sub/x1", "1");
        f.tmp.write("work/sub/x2", "2");
        f.tmp.write("work/sub/x3", "3");
        const std::size_t outsideBefore = countAll(f.dir("outside"));

        std::size_t calls = 0;
        f.cleaner.setBeforeRemoveHook([&](const fs::path&) {
            if (++calls == trigger) {
                fs::rename(work / "sub", work / "sub_moved");
                fs::create_directory_symlink(f.dir("outside"), work / "sub");
            }
        });

        const SafeCleaner::Report report = f.cleaner.clean(makeRule(work));
        const std::size_t removedBefore = trigger - 1;
        CHECK(report.outcome == (trigger == 1 ? Outcome::Failed : Outcome::Interrupted));
        CHECK_EQ(calls, trigger);
        CHECK_EQ(report.removedFiles, removedBefore);
        CHECK_EQ(report.removedDirs, 0u);
        CHECK(anyMessage(report, "directory changed during cleanup"));
        CHECK_EQ(countAll(work / "sub_moved"), 3 - removedBefore);
        CHECK_EQ(countAll(f.dir("outside")), outsideBefore);
        CHECK(present(f.dir("outside/precious.txt")));
    }
}

void testSymlinksInsideAreNotFollowed() {
    Fixture f;
    const fs::path work = f.dir("work");
    fs::create_directories(work / "sub");
    fs::create_directory_symlink(f.dir("outside"), work / "link_dir");
    fs::create_symlink(f.dir("outside") / "precious.txt", work / "link_file");
    fs::create_symlink("missing_target", work / "dangling");
    fs::create_directory_symlink(f.tmp.path(), work / "link_up");
    fs::create_directory_symlink(f.dir("fake_etc"), work / "sub" / "link_etc");

    const SafeCleaner::Report report = f.cleaner.clean(makeRule(work));
    CHECK(report.outcome == Outcome::Cleaned);
    CHECK_EQ(countEntries(work), 0u);
    CHECK_EQ(report.removedFiles, 5u);
    CHECK_EQ(report.removedDirs, 1u);
    CHECK(present(f.dir("outside/precious.txt")));
    CHECK(present(f.dir("fake_etc/passwd")));
    CHECK(present(f.dir("fake_etc/inner/shadow")));
    CHECK(present(f.dir("fake_home/doc.txt")));
    CHECK(present(f.dir("app/sub/daemon.conf")));
}

void testFolderIsSymlink() {
    Fixture f;
    const fs::path real = f.dir("real");
    fs::create_directories(real / "sub");
    f.tmp.write("real/a", "a");
    f.tmp.write("real/sub/b", "b");
    fs::create_directory_symlink(real, f.dir("link"));

    const SafeCleaner::Report report = f.cleaner.clean(makeRule(f.dir("link")));
    CHECK(report.outcome == Outcome::Cleaned);
    CHECK(report.folder == real);
    CHECK_EQ(countEntries(real), 0u);
    CHECK(fs::is_symlink(f.dir("link")));
    CHECK(fs::is_directory(real));

    fs::create_directory_symlink(f.dir("fake_etc"), f.dir("etc_link"));
    const SafeCleaner::Report rejected = f.cleaner.clean(makeRule(f.dir("etc_link")));
    CHECK(rejected.outcome == Outcome::Rejected);
    CHECK(present(f.dir("fake_etc/passwd")));
    CHECK(present(f.dir("fake_etc/inner/shadow")));
}

void testRejectedFolders() {
    Fixture f;
    const std::vector<std::string> rejected = {"fake_etc", "fake_etc/inner", "fake_home",
                                               "fake_home/.ssh", "fake_exact", "app/sub",
                                               "app"};
    for (const std::string& relative : rejected) {
        const SafeCleaner::Report report = f.cleaner.clean(makeRule(f.dir(relative)));
        CHECK(report.outcome == Outcome::Rejected);
        CHECK_EQ(report.messages.size(), 1u);
        CHECK_EQ(report.removedFiles, 0u);
    }
    CHECK(f.cleaner.clean(makeRule(f.tmp.path())).outcome == Outcome::Rejected);

    CHECK(present(f.dir("fake_etc/passwd")));
    CHECK(present(f.dir("fake_etc/inner/shadow")));
    CHECK(present(f.dir("fake_home/doc.txt")));
    CHECK(present(f.dir("fake_home/.ssh/key")));
    CHECK(present(f.dir("fake_exact/file")));
    CHECK(present(f.dir("fake_exact/child")));
    CHECK(present(f.dir("app/sub/daemon.conf")));
    CHECK(present(f.dir("app/sub/daemon.pid")));
    CHECK(present(f.dir("app/sub/daemon_bin")));
    CHECK(present(f.dir("outside/precious.txt")));

    for (const char* relative : {"fake_exact/child", "fake_home/work", "app/other"}) {
        f.tmp.write(std::string(relative) + "/junk", "j");
        const SafeCleaner::Report report = f.cleaner.clean(makeRule(f.dir(relative)));
        CHECK(report.outcome == Outcome::Cleaned);
        CHECK_EQ(countEntries(f.dir(relative)), 0u);
    }
    CHECK(present(f.dir("fake_exact/file")));
    CHECK(present(f.dir("fake_home/doc.txt")));
}

void testInvalidRules() {
    Fixture f;
    const fs::path work = f.dir("work");
    fs::create_directories(work);
    f.tmp.write("work/data", "d");

    for (const char* bad : {"", "a/b", "..", ".", "/abs"}) {
        const SafeCleaner::Report report = f.cleaner.clean(makeRule(work, bad));
        CHECK(report.outcome == Outcome::Rejected);
        CHECK(present(work / "data"));
    }

    CHECK(f.cleaner.clean(makeRule(f.dir("missing"))).outcome == Outcome::Rejected);
    CHECK(!present(f.dir("missing")));

    f.tmp.write("plain.txt", "p");
    CHECK(f.cleaner.clean(makeRule(f.dir("plain.txt"))).outcome == Outcome::Rejected);
    CHECK(present(f.dir("plain.txt")));

    CHECK(f.cleaner.clean(makeRule("relative/dir")).outcome == Outcome::Rejected);
}

void testValidate() {
    Fixture f;
    fs::create_directories(f.dir("work"));
    fs::create_directories(f.dir("work2"));

    CHECK(f.cleaner.validate({makeRule(f.dir("work"))}).empty());

    const std::vector<Config::Rule> rules = {makeRule(f.dir("work")),
                                             makeRule(f.dir("fake_etc")),
                                             makeRule(f.dir("missing")),
                                             makeRule(f.dir("work2"), "a/b")};
    const std::vector<std::string> errors = f.cleaner.validate(rules);
    REQUIRE(errors.size() == 3);
    CHECK(contains(errors[0], "fake_etc"));
    CHECK(contains(errors[0], "protected"));
    CHECK(contains(errors[1], "missing"));
    CHECK(contains(errors[1], "cannot resolve"));
    CHECK(contains(errors[2], "invalid protective file name"));
}

void testOverlappingRules() {
    Fixture f;
    fs::create_directories(f.dir("a"));
    fs::create_directories(f.dir("ab"));
    fs::create_directories(f.dir("p/child"));
    f.tmp.write("a/x");
    f.tmp.write("ab/y");
    f.tmp.write("p/file");
    f.tmp.write("p/child/file2");

    const std::vector<Config::Rule> rules = {makeRule(f.dir("a")), makeRule(f.dir("ab")),
                                             makeRule(f.dir("p")), makeRule(f.dir("p/child"))};
    const std::vector<std::string> errors = f.cleaner.validate(rules);
    CHECK_EQ(errors.size(), 2u);
    for (const std::string& error : errors) {
        CHECK(contains(error, "overlaps"));
    }

    const std::vector<SafeCleaner::Report> reports = f.cleaner.cleanAll(rules);
    REQUIRE(reports.size() == 4);
    CHECK(reports[0].outcome == Outcome::Cleaned);
    CHECK(reports[1].outcome == Outcome::Cleaned);
    CHECK(reports[2].outcome == Outcome::Rejected);
    CHECK(reports[3].outcome == Outcome::Rejected);
    CHECK(!present(f.dir("a/x")));
    CHECK(!present(f.dir("ab/y")));
    CHECK(present(f.dir("p/file")));
    CHECK(present(f.dir("p/child/file2")));

    fs::create_directories(f.dir("s"));
    f.tmp.write("s/x");
    fs::create_directory_symlink(f.dir("s"), f.dir("s_link"));
    const std::vector<SafeCleaner::Report> same =
        f.cleaner.cleanAll({makeRule(f.dir("s")), makeRule(f.dir("s_link"))});
    REQUIRE(same.size() == 2);
    CHECK(same[0].outcome == Outcome::Rejected);
    CHECK(same[1].outcome == Outcome::Rejected);
    CHECK(present(f.dir("s/x")));
}

void testPathChangedAfterValidation() {
    Fixture f;
    fs::create_directories(f.dir("safe"));
    fs::create_directories(f.dir("a/sub"));
    f.tmp.write("safe/x");
    f.tmp.write("a/top");
    f.tmp.write("a/sub/z");
    fs::create_directory_symlink(f.dir("safe"), f.dir("moving"));

    const std::vector<Config::Rule> single = {makeRule(f.dir("moving"))};
    CHECK(f.cleaner.validate(single).empty());
    fs::remove(f.dir("moving"));
    fs::create_directory_symlink(f.dir("fake_etc"), f.dir("moving"));
    const std::vector<SafeCleaner::Report> first = f.cleaner.cleanAll(single);
    REQUIRE(first.size() == 1);
    CHECK(first[0].outcome == Outcome::Rejected);
    CHECK(present(f.dir("fake_etc/passwd")));
    CHECK(present(f.dir("safe/x")));

    fs::remove(f.dir("moving"));
    fs::create_directory_symlink(f.dir("safe"), f.dir("moving"));
    const std::vector<Config::Rule> pair = {makeRule(f.dir("a")), makeRule(f.dir("moving"))};
    CHECK(f.cleaner.validate(pair).empty());
    fs::remove(f.dir("moving"));
    fs::create_directory_symlink(f.dir("a/sub"), f.dir("moving"));
    const std::vector<SafeCleaner::Report> second = f.cleaner.cleanAll(pair);
    REQUIRE(second.size() == 2);
    CHECK(second[0].outcome == Outcome::Rejected);
    CHECK(second[1].outcome == Outcome::Rejected);
    CHECK(present(f.dir("a/top")));
    CHECK(present(f.dir("a/sub/z")));
}

void testSpecialNamesAndBulk() {
    Fixture f;
    const fs::path work = f.dir("work");
    fs::create_directories(work / "bulk");
    for (const char* name : {"with space", "percent%s%n", "-rf", "-", "line\nbreak", "тест.txt"}) {
        f.tmp.write(std::string("work/") + name, "x");
    }
    CHECK(::mkfifo((work / "pipe").c_str(), 0600) == 0);
    for (int i = 0; i < 300; ++i) {
        f.tmp.write("work/bulk/f" + std::to_string(i), "x");
    }

    const SafeCleaner::Report report = f.cleaner.clean(makeRule(work));
    CHECK(report.outcome == Outcome::Cleaned);
    CHECK_EQ(countEntries(work), 0u);
    CHECK_EQ(report.removedFiles, 307u);
    CHECK_EQ(report.removedDirs, 1u);
}

void testRepeatedCleaning() {
    Fixture f;
    const fs::path work = f.dir("work");
    fs::create_directories(work);

    const SafeCleaner::Report empty = f.cleaner.clean(makeRule(work));
    CHECK(empty.outcome == Outcome::Cleaned);
    CHECK_EQ(empty.removedFiles, 0u);
    CHECK_EQ(empty.removedDirs, 0u);
    CHECK(empty.removedTopLevel.empty());

    fs::create_directories(work / "dir");
    f.tmp.write("work/new1");
    f.tmp.write("work/dir/new2");
    const SafeCleaner::Report again = f.cleaner.clean(makeRule(work));
    CHECK(again.outcome == Outcome::Cleaned);
    CHECK_EQ(again.removedFiles, 2u);
    CHECK_EQ(again.removedDirs, 1u);
    CHECK_EQ(countEntries(work), 0u);
}

void testUnverifiableIgnfile() {
    if (geteuid() == 0) {
        return;
    }
    Fixture f;
    const fs::path work = f.dir("work");
    fs::create_directories(work);
    f.tmp.write("work/data", "d");

    fs::permissions(work, fs::perms::none);
    const SafeCleaner::Report report = f.cleaner.clean(makeRule(work));
    fs::permissions(work, fs::perms::owner_all);

    CHECK(report.outcome == Outcome::Failed);
    CHECK(!report.messages.empty());
    CHECK(contains(report.messages[0], "cannot verify"));
    CHECK_EQ(report.removedFiles, 0u);
    CHECK(present(work / "data"));
}

void testPartialFailureDoesNotStopOthers() {
    if (geteuid() == 0) {
        return;
    }
    Fixture f;
    const fs::path work = f.dir("work");
    fs::create_directories(work / "locked");
    fs::create_directories(work / "sub");
    f.tmp.write("work/free.txt", "f");
    f.tmp.write("work/sub/ok", "o");
    for (int i = 0; i < 30; ++i) {
        f.tmp.write("work/locked/f" + std::to_string(i), "x");
    }

    fs::permissions(work / "locked", fs::perms::owner_read | fs::perms::owner_exec);
    const SafeCleaner::Report report = f.cleaner.clean(makeRule(work));
    fs::permissions(work / "locked", fs::perms::owner_all);

    CHECK(report.outcome == Outcome::Failed);
    CHECK(!report.messages.empty());
    CHECK(report.messages.size() <= 21);
    CHECK(contains(report.messages.back(), "suppressed"));
    CHECK(!present(work / "free.txt"));
    CHECK(!present(work / "sub"));
    CHECK(present(work / "locked"));
    CHECK(present(work / "locked" / "f0"));
    CHECK_EQ(report.removedFiles, 2u);
    CHECK_EQ(report.removedDirs, 1u);

    const SafeCleaner::Report second = f.cleaner.clean(makeRule(work));
    CHECK(second.outcome == Outcome::Cleaned);
    CHECK_EQ(countEntries(work), 0u);
}

}

int main() {
    testStandardPolicy();
    testStandardPolicyProtectsSymlinkedConfig();
    testWindowsDrivesParsing();
    testParsedWindowsDriveProtectsWholeTree();
    testStandardPolicyBlocksWindowsDriveTrees();
    testCleansEverythingInside();
    testIgnfileProtects();
    testIgnfileOnlyCountsInsideFolderItself();
    testIgnfileAppearsDuringCleanup();
    testIgnfileAppearsBeforeFirstRemoval();
    testDirectoryReplacedDuringCleanup();
    testSymlinksInsideAreNotFollowed();
    testFolderIsSymlink();
    testRejectedFolders();
    testInvalidRules();
    testValidate();
    testOverlappingRules();
    testPathChangedAfterValidation();
    testSpecialNamesAndBulk();
    testRepeatedCleaning();
    testUnverifiableIgnfile();
    testPartialFailureDoesNotStopOthers();
    return check::summary("unit_cleaner");
}