#include "PidFile.h"
#include "check.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

namespace fs = std::filesystem;

namespace {

using Kind = PidFile::Previous::Kind;
using State = PidFile::ProcState;

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

void writePid(const fs::path& path, pid_t pid) {
    std::ofstream out(path, std::ios::trunc);
    out << pid << '\n';
}

std::uintmax_t linkCount(const fs::path& path) {
    std::error_code ec;
    const std::uintmax_t count = fs::hard_link_count(path, ec);
    return ec ? 0 : count;
}

bool waitForState(pid_t pid, State wanted, int timeoutMs) {
    for (int waited = 0; waited <= timeoutMs; waited += 20) {
        if (PidFile::inspect(pid) == wanted) {
            return true;
        }
        ::usleep(20000);
    }
    return false;
}

bool isRunning(pid_t pid) {
    const State state = PidFile::inspect(pid);
    return state == State::Ours || state == State::Foreign;
}

void killAndReap(pid_t pid) {
    if (pid <= 0) {
        return;
    }
    ::kill(pid, SIGKILL);
    int status = 0;
    ::waitpid(pid, &status, 0);
}

bool reapTerminated(pid_t pid, int& status) {
    for (int waited = 0; waited < 3000; waited += 20) {
        const pid_t result = ::waitpid(pid, &status, WNOHANG);
        if (result == pid) {
            return true;
        }
        if (result < 0) {
            return false;
        }
        ::usleep(20000);
    }
    ::kill(pid, SIGKILL);
    ::waitpid(pid, &status, 0);
    return false;
}

pid_t spawnWaiter() {
    std::cout.flush();
    const pid_t pid = ::fork();
    if (pid == 0) {
        for (;;) {
            ::pause();
        }
    }
    return pid;
}

pid_t spawnSleeper() {
    std::cout.flush();
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::execlp("sleep", "sleep", "60", static_cast<char*>(nullptr));
        ::_exit(127);
    }
    if (pid > 0) {
        waitForState(pid, State::Foreign, 3000);
    }
    return pid;
}

pid_t deadPid() {
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::_exit(0);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return pid;
}

pid_t spawnHolder(const fs::path& path, bool ignoreTerm) {
    int ready[2];
    if (::pipe(ready) != 0) {
        return -1;
    }
    std::cout.flush();
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(ready[0]);
        ::close(ready[1]);
        return -1;
    }
    if (pid == 0) {
        ::close(ready[0]);
        if (ignoreTerm) {
            ::signal(SIGTERM, SIG_IGN);
        }
        PidFile file(path);
        std::string error;
        const char status = file.acquire(error) ? 'K' : 'E';
        const ssize_t n = ::write(ready[1], &status, 1);
        static_cast<void>(n);
        ::close(ready[1]);
        if (status != 'K') {
            ::_exit(3);
        }
        for (;;) {
            ::pause();
        }
    }
    ::close(ready[1]);
    char status = 0;
    ssize_t n;
    do {
        n = ::read(ready[0], &status, 1);
    } while (n < 0 && errno == EINTR);
    ::close(ready[0]);
    if (n != 1 || status != 'K') {
        killAndReap(pid);
        return -1;
    }
    return pid;
}

class ExternalLock {
public:
    ExternalLock(const fs::path& path, const std::string& content) {
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
        if (fd_ < 0) {
            return;
        }
        if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            ::close(fd_);
            fd_ = -1;
            return;
        }
        const ssize_t n = ::write(fd_, content.data(), content.size());
        if (n != static_cast<ssize_t>(content.size())) {
            ::close(fd_);
            fd_ = -1;
        }
    }

    ~ExternalLock() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }

    ExternalLock(const ExternalLock&) = delete;
    ExternalLock& operator=(const ExternalLock&) = delete;

    bool ok() const { return fd_ >= 0; }

private:
    int fd_ = -1;
};

void testParsePid() {
    pid_t pid = 0;
    CHECK(PidFile::parsePid("1234\n", pid));
    CHECK_EQ(pid, 1234);
    CHECK(PidFile::parsePid("2", pid));
    CHECK_EQ(pid, 2);
    CHECK(PidFile::parsePid("4194304\r\n", pid));
    CHECK_EQ(pid, 4194304);
    CHECK(PidFile::parsePid("77  \n", pid));
    CHECK_EQ(pid, 77);

    pid = 55;
    const std::string nul("12\0x", 4);
    for (const std::string& bad :
         {std::string(), std::string("\n"), std::string("abc"), std::string("-5"),
          std::string("0"), std::string("1"), std::string("1\n"), std::string("12 34"),
          std::string("12\n34\n"), std::string("4194305"), std::string("99999999999"),
          std::string("12abc"), std::string(" 12"), std::string("+12"), nul}) {
        CHECK(!PidFile::parsePid(bad, pid));
        CHECK_EQ(pid, 55);
    }
}

void testStripDeletedSuffix() {
    CHECK_EQ(PidFile::stripDeletedSuffix("/a/b (deleted)"), "/a/b");
    CHECK_EQ(PidFile::stripDeletedSuffix("/a/b"), "/a/b");
    CHECK_EQ(PidFile::stripDeletedSuffix(" (deleted)"), " (deleted)");
    CHECK_EQ(PidFile::stripDeletedSuffix("/a (deleted) b"), "/a (deleted) b");
}

void testInspect() {
    CHECK(PidFile::inspect(::getpid()) == State::Ours);
    CHECK(PidFile::inspect(0) == State::Missing);
    CHECK(PidFile::inspect(-1) == State::Missing);
    CHECK(PidFile::inspect(1) == State::Foreign);

    const pid_t twin = spawnWaiter();
    REQUIRE(twin > 0);
    CHECK(PidFile::inspect(twin) == State::Ours);
    ::kill(twin, SIGKILL);
    CHECK(waitForState(twin, State::Zombie, 3000));
    int status = 0;
    ::waitpid(twin, &status, 0);
    CHECK(PidFile::inspect(twin) == State::Missing);

    const pid_t sleeper = spawnSleeper();
    REQUIRE(sleeper > 0);
    CHECK(PidFile::inspect(sleeper) == State::Foreign);
    killAndReap(sleeper);
    CHECK(PidFile::inspect(sleeper) == State::Missing);
}

void testLockHeldByParsing() {
    const dev_t dev = makedev(8, 1);
    const ino_t ino = 600;

    std::string text;
    text += "1: POSIX  ADVISORY  WRITE 111 08:01:600 0 EOF\n";
    text += "2: FLOCK  ADVISORY  WRITE 222 08:01:500 0 EOF\n";
    text += "3: FLOCK  ADVISORY  WRITE 333 08:01:600 0 EOF\n";
    text += "3: -> FLOCK  ADVISORY  WRITE 444 08:01:600 0 EOF\n";
    text += "3:  ->FLOCK  ADVISORY  WRITE 445 08:01:600 0 EOF\n";
    text += "4: FLOCK  ADVISORY  READ  555 08:01:600 0 EOF\n";
    text += "5: FLOCK  ADVISORY  WRITE 666 08:02:600 0 EOF\n";
    text += "6: FLOCK  ADVISORY  WRITE 777 <none>:0 0 EOF\n";
    text += "7: OFDLCK ADVISORY  WRITE -1 08:01:600 0 EOF\n";
    text += "garbage line\n\n";
    text += "8: FLOCK  ADVISORY  WRITE\n";
    text += "9: FLOCK  ADVISORY  WRITE 888 08:01:600:7 0 EOF\n";

    CHECK(PidFile::lockHeldBy(text, 333, dev, ino));
    CHECK(!PidFile::lockHeldBy(text, 111, dev, ino));
    CHECK(!PidFile::lockHeldBy(text, 222, dev, ino));
    CHECK(!PidFile::lockHeldBy(text, 444, dev, ino));
    CHECK(!PidFile::lockHeldBy(text, 445, dev, ino));
    CHECK(!PidFile::lockHeldBy(text, 555, dev, ino));
    CHECK(!PidFile::lockHeldBy(text, 666, dev, ino));
    CHECK(!PidFile::lockHeldBy(text, 777, dev, ino));
    CHECK(!PidFile::lockHeldBy(text, 888, dev, ino));
    CHECK(!PidFile::lockHeldBy(text, 3, dev, ino));
    CHECK(!PidFile::lockHeldBy(text, 0, dev, ino));
    CHECK(!PidFile::lockHeldBy(text, -1, dev, ino));
    CHECK(!PidFile::lockHeldBy(text, 333, dev, 601));
    CHECK(!PidFile::lockHeldBy(text, 333, makedev(8, 2), ino));
    CHECK(!PidFile::lockHeldBy(text, 333, makedev(9, 1), ino));
    CHECK(!PidFile::lockHeldBy("", 333, dev, ino));

    const std::string wide = "1: FLOCK  ADVISORY  WRITE 42 103:1f:123456789012 0 EOF\n";
    CHECK(PidFile::lockHeldBy(wide, 42, makedev(259, 31), 123456789012UL));
    CHECK(!PidFile::lockHeldBy(wide, 42, makedev(259, 30), 123456789012UL));
    CHECK(!PidFile::lockHeldBy(wide, 42, makedev(3, 31), 123456789012UL));
}

void testLockHeldByMatchesRealKernelLocks() {
    TempDir tmp;
    tmp.mkdir("run");
    const fs::path path = tmp.path() / "run" / "d.pid";
    ExternalLock lock(path, "x\n");
    REQUIRE(lock.ok());

    struct stat st;
    REQUIRE(::stat(path.c_str(), &st) == 0);
    const std::string locks = slurp("/proc/locks");
    REQUIRE(!locks.empty());
    CHECK(PidFile::lockHeldBy(locks, ::getpid(), st.st_dev, st.st_ino));
    CHECK(!PidFile::lockHeldBy(locks, ::getppid(), st.st_dev, st.st_ino));
    CHECK(!PidFile::lockHeldBy(locks, ::getpid(), st.st_dev, st.st_ino + 1));
}

void testAcquireAndRelease() {
    TempDir tmp;
    tmp.mkdir("run");
    const fs::path path = tmp.path() / "run" / "d.pid";
    const std::string expected = std::to_string(::getpid()) + "\n";
    std::string error;
    {
        PidFile first(path);
        REQUIRE(first.acquire(error));
        CHECK(error.empty());
        CHECK(first.held());
        CHECK_EQ(slurp(path), expected);

        struct stat st;
        REQUIRE(::stat(path.c_str(), &st) == 0);
        CHECK(S_ISREG(st.st_mode));
        CHECK(st.st_uid == ::geteuid());

        PidFile second(path);
        CHECK(!second.acquire(error));
        CHECK(contains(error, "already running"));
        CHECK(!second.held());
        CHECK_EQ(slurp(path), expected);

        CHECK(!first.acquire(error));
        CHECK(contains(error, "already held"));
    }
    CHECK(!present(path));

    PidFile again(path);
    CHECK(again.acquire(error));
    again.release();
    CHECK(!again.held());
    CHECK(!present(path));
}

void testReleaseKeepsForeignFile() {
    TempDir tmp;
    tmp.mkdir("run");
    const fs::path path = tmp.path() / "run" / "d.pid";
    std::string error;

    {
        PidFile pidFile(path);
        REQUIRE(pidFile.acquire(error));
        fs::remove(path);
        tmp.write("run/d.pid", "other\n");
        pidFile.release();
        CHECK(!pidFile.held());
        CHECK_EQ(slurp(path), "other\n");
    }

    fs::remove(path);
    {
        PidFile pidFile(path);
        REQUIRE(pidFile.acquire(error));
        std::ofstream(path) << "999\n";
        pidFile.release();
        CHECK_EQ(slurp(path), "999\n");
    }
}

void testAcquireRefusesBadTargets() {
    TempDir tmp;
    tmp.mkdir("run");
    std::string error;

    const fs::path target = tmp.write("target.txt", "keep\n");
    const fs::path link = tmp.path() / "run" / "link.pid";
    fs::create_symlink(target, link);
    PidFile viaLink(link);
    CHECK(!viaLink.acquire(error));
    CHECK(contains(error, "symbolic link"));
    CHECK_EQ(slurp(target), "keep\n");

    const fs::path dangling = tmp.path() / "run" / "dangling.pid";
    fs::create_symlink(tmp.path() / "nowhere.txt", dangling);
    PidFile viaDangling(dangling);
    CHECK(!viaDangling.acquire(error));
    CHECK(contains(error, "symbolic link"));
    CHECK(!present(tmp.path() / "nowhere.txt"));

    PidFile onDirectory(tmp.mkdir("dir.pid"));
    CHECK(!onDirectory.acquire(error));
    CHECK(!error.empty());

    const fs::path fifo = tmp.path() / "run" / "fifo.pid";
    REQUIRE(::mkfifo(fifo.c_str(), 0600) == 0);
    PidFile onFifo(fifo);
    CHECK(!onFifo.acquire(error));
    CHECK(contains(error, "not a regular file"));

    PidFile missingDir(tmp.path() / "no_such_dir" / "x.pid");
    CHECK(!missingDir.acquire(error));
    CHECK(contains(error, "cannot open"));

    PidFile relative("relative.pid");
    CHECK(!relative.acquire(error));
    CHECK(contains(error, "not absolute"));
}

void testAcquireRefusesHardLinkedFile() {
    TempDir tmp;
    tmp.mkdir("run");
    const fs::path victim = tmp.write("victim.txt", "keep\n");
    const fs::path path = tmp.path() / "run" / "d.pid";
    std::error_code ec;
    fs::create_hard_link(victim, path, ec);
    REQUIRE(!ec);
    REQUIRE(linkCount(victim) == 2);

    std::string error;
    {
        PidFile pidFile(path);
        CHECK(!pidFile.acquire(error));
        CHECK(contains(error, "hard link"));
        CHECK(!pidFile.held());
    }
    CHECK_EQ(slurp(victim), "keep\n");
    CHECK_EQ(slurp(path), "keep\n");
    CHECK(present(victim));
    CHECK(present(path));
    CHECK(linkCount(victim) == 2);

    const PidFile::Previous previous = PidFile(path).stopPrevious(200);
    CHECK(previous.kind == Kind::Failed);
    CHECK(contains(previous.message, "hard link"));
    CHECK_EQ(slurp(victim), "keep\n");

    fs::remove(victim);
    CHECK(linkCount(path) == 1);
    PidFile single(path);
    CHECK(single.acquire(error));
    CHECK_EQ(slurp(path), std::to_string(::getpid()) + "\n");
}

void testStopPreviousNoFileAndStale() {
    TempDir tmp;
    tmp.mkdir("run");
    const fs::path path = tmp.path() / "run" / "d.pid";
    const PidFile pidFile(path);

    PidFile::Previous result = pidFile.stopPrevious(200);
    CHECK(result.kind == Kind::None);

    const pid_t dead = deadPid();
    tmp.write("run/d.pid", std::to_string(dead) + "\n");
    result = pidFile.stopPrevious(200);
    CHECK(result.kind == Kind::Stale);
    CHECK_EQ(result.pid, dead);
    CHECK(contains(result.message, "stale"));

    for (const char* junk : {"", "\n", "hello\n", "12 34\n", "-7\n", "999999999999\n"}) {
        tmp.write("run/d.pid", junk);
        result = pidFile.stopPrevious(200);
        CHECK(result.kind == Kind::Stale);
        CHECK_EQ(result.pid, 0);
    }

    const pid_t sleeper = spawnSleeper();
    REQUIRE(sleeper > 0);
    tmp.write("run/d.pid", std::to_string(sleeper) + "\n");
    result = pidFile.stopPrevious(200);
    CHECK(result.kind == Kind::Stale);
    CHECK(isRunning(sleeper));
    killAndReap(sleeper);

    const pid_t twin = spawnWaiter();
    REQUIRE(twin > 0);
    tmp.write("run/d.pid", std::to_string(twin) + "\n");
    result = pidFile.stopPrevious(200);
    CHECK(result.kind == Kind::Stale);
    CHECK(isRunning(twin));
    killAndReap(twin);
}

void testStopPreviousRefusesBadFiles() {
    TempDir tmp;
    tmp.mkdir("run");

    const fs::path target = tmp.write("target.txt", "keep\n");
    const fs::path link = tmp.path() / "run" / "link.pid";
    fs::create_symlink(target, link);
    PidFile::Previous result = PidFile(link).stopPrevious(200);
    CHECK(result.kind == Kind::Failed);
    CHECK(contains(result.message, "symbolic link"));
    CHECK_EQ(slurp(target), "keep\n");

    result = PidFile(tmp.mkdir("dir.pid")).stopPrevious(200);
    CHECK(result.kind == Kind::Failed);
    CHECK(contains(result.message, "not a regular file"));

    result = PidFile("rel.pid").stopPrevious(200);
    CHECK(result.kind == Kind::Failed);
    CHECK(contains(result.message, "not absolute"));
}

void testStopPreviousRefusesUnidentifiedHolder() {
    TempDir tmp;
    tmp.mkdir("run");
    const fs::path path = tmp.path() / "run" / "d.pid";
    const PidFile pidFile(path);

    {
        ExternalLock lock(path, "garbage\n");
        REQUIRE(lock.ok());
        const PidFile::Previous result = pidFile.stopPrevious(200);
        CHECK(result.kind == Kind::Failed);
        CHECK(contains(result.message, "valid PID"));
    }

    const pid_t sleeper = spawnSleeper();
    REQUIRE(sleeper > 0);
    {
        ExternalLock lock(path, std::to_string(sleeper) + "\n");
        REQUIRE(lock.ok());
        const PidFile::Previous result = pidFile.stopPrevious(200);
        CHECK(result.kind == Kind::Failed);
        CHECK(contains(result.message, "not an instance"));
        CHECK(isRunning(sleeper));
    }
    killAndReap(sleeper);

    const pid_t dead = deadPid();
    {
        ExternalLock lock(path, std::to_string(dead) + "\n");
        REQUIRE(lock.ok());
        const PidFile::Previous result = pidFile.stopPrevious(200);
        CHECK(result.kind == Kind::Failed);
        CHECK(contains(result.message, "not running"));
    }

    const pid_t twin = spawnWaiter();
    REQUIRE(twin > 0);
    {
        ExternalLock lock(path, std::to_string(twin) + "\n");
        CHECK(lock.ok());
        const PidFile::Previous result = pidFile.stopPrevious(200);
        CHECK(result.kind == Kind::Failed);
        CHECK_EQ(result.pid, twin);
        CHECK(contains(result.message, "not held by"));
        CHECK(contains(result.message, "no signal sent"));
        CHECK(isRunning(twin));
    }
    killAndReap(twin);

    {
        ExternalLock lock(path, std::to_string(::getpid()) + "\n");
        REQUIRE(lock.ok());
        const PidFile::Previous result = pidFile.stopPrevious(200);
        CHECK(result.kind == Kind::Failed);
        CHECK(contains(result.message, "this very process"));
    }
}

void testStopPreviousRefusesPidOfNonLockHolder() {
    TempDir tmp;
    tmp.mkdir("run");
    const fs::path path = tmp.path() / "run" / "d.pid";

    const pid_t holder = spawnHolder(path, false);
    REQUIRE(holder > 0);
    const pid_t victim = spawnWaiter();
    if (victim <= 0) {
        killAndReap(holder);
    }
    REQUIRE(victim > 0);

    struct stat st{};
    CHECK(::stat(path.c_str(), &st) == 0);
    const std::string locks = slurp("/proc/locks");
    CHECK(PidFile::lockHeldBy(locks, holder, st.st_dev, st.st_ino));
    CHECK(!PidFile::lockHeldBy(locks, victim, st.st_dev, st.st_ino));

    writePid(path, victim);
    CHECK_EQ(slurp(path), std::to_string(victim) + "\n");
    CHECK(PidFile::inspect(victim) == State::Ours);

    const PidFile pidFile(path);
    PidFile::Previous result = pidFile.stopPrevious(300);
    CHECK(result.kind == Kind::Failed);
    CHECK_EQ(result.pid, victim);
    CHECK(contains(result.message, "not held by"));
    CHECK(contains(result.message, "no signal sent"));
    CHECK(isRunning(victim));
    CHECK(PidFile::inspect(victim) == State::Ours);
    CHECK(::kill(victim, 0) == 0);
    CHECK(isRunning(holder));
    CHECK(PidFile::lockHeldBy(slurp("/proc/locks"), holder, st.st_dev, st.st_ino));

    writePid(path, holder);
    result = pidFile.stopPrevious(3000);
    CHECK(result.kind == Kind::Stopped);
    CHECK_EQ(result.pid, holder);
    int status = 0;
    CHECK(reapTerminated(holder, status));
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
    CHECK(PidFile::inspect(victim) == State::Ours);
    killAndReap(victim);
}

void testStopPreviousStopsHolder() {
    TempDir tmp;
    tmp.mkdir("run");
    const fs::path path = tmp.path() / "run" / "d.pid";

    const pid_t holder = spawnHolder(path, false);
    REQUIRE(holder > 0);
    CHECK_EQ(slurp(path), std::to_string(holder) + "\n");

    const PidFile pidFile(path);
    const PidFile::Previous result = pidFile.stopPrevious(3000);
    CHECK(result.kind == Kind::Stopped);
    CHECK_EQ(result.pid, holder);

    int status = 0;
    CHECK(reapTerminated(holder, status));
    CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);

    std::string error;
    PidFile next(path);
    CHECK(next.acquire(error));
    CHECK_EQ(slurp(path), std::to_string(::getpid()) + "\n");
}

void testStopPreviousReplacesRepeatedly() {
    TempDir tmp;
    tmp.mkdir("run");
    const fs::path path = tmp.path() / "run" / "d.pid";
    const PidFile pidFile(path);

    for (int round = 0; round < 3; ++round) {
        const pid_t holder = spawnHolder(path, false);
        REQUIRE(holder > 0);
        CHECK_EQ(slurp(path), std::to_string(holder) + "\n");

        const PidFile::Previous result = pidFile.stopPrevious(3000);
        CHECK(result.kind == Kind::Stopped);
        CHECK_EQ(result.pid, holder);

        int status = 0;
        CHECK(reapTerminated(holder, status));
        CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGTERM);
    }
}

void testStopPreviousTimeout() {
    TempDir tmp;
    tmp.mkdir("run");
    const fs::path path = tmp.path() / "run" / "d.pid";

    const pid_t holder = spawnHolder(path, true);
    REQUIRE(holder > 0);

    const PidFile pidFile(path);
    const PidFile::Previous result = pidFile.stopPrevious(300);
    CHECK(result.kind == Kind::Failed);
    CHECK_EQ(result.pid, holder);
    CHECK(contains(result.message, "did not exit"));
    CHECK(isRunning(holder));
    killAndReap(holder);
}

}

int main() {
    testParsePid();
    testStripDeletedSuffix();
    testInspect();
    testLockHeldByParsing();
    testLockHeldByMatchesRealKernelLocks();
    testAcquireAndRelease();
    testReleaseKeepsForeignFile();
    testAcquireRefusesBadTargets();
    testAcquireRefusesHardLinkedFile();
    testStopPreviousNoFileAndStale();
    testStopPreviousRefusesBadFiles();
    testStopPreviousRefusesUnidentifiedHolder();
    testStopPreviousRefusesPidOfNonLockHolder();
    testStopPreviousStopsHolder();
    testStopPreviousReplacesRepeatedly();
    testStopPreviousTimeout();
    return check::summary("unit_pidfile");
}
