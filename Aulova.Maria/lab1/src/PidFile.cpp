#include "PidFile.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

namespace {

constexpr long long kMaxPid = 4194304;
constexpr std::size_t kLinkBuffer = 4096;
constexpr int kPollUs = 50000;
constexpr int kReadRetries = 10;
constexpr int kReadRetryUs = 20000;

class Fd {
public:
    explicit Fd(int fd = -1) : fd_(fd) {}
    ~Fd() {
        if (fd_ >= 0) {
            ::close(fd_);
        }
    }
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;

    int get() const { return fd_; }
    int release() {
        const int fd = fd_;
        fd_ = -1;
        return fd;
    }

private:
    int fd_;
};

std::string errText(int err) {
    return std::generic_category().message(err);
}

PidFile::Previous make(PidFile::Previous::Kind kind, pid_t pid, std::string message) {
    PidFile::Previous result;
    result.kind = kind;
    result.pid = pid;
    result.message = std::move(message);
    return result;
}

std::string readHead(int fd) {
    char buffer[64];
    ssize_t n;
    do {
        n = ::pread(fd, buffer, sizeof buffer, 0);
    } while (n < 0 && errno == EINTR);
    return n > 0 ? std::string(buffer, static_cast<std::size_t>(n)) : std::string();
}

bool readLink(const std::string& path, std::string& target) {
    char buffer[kLinkBuffer];
    const ssize_t n = ::readlink(path.c_str(), buffer, sizeof buffer);
    if (n < 0 || static_cast<std::size_t>(n) >= sizeof buffer) {
        return false;
    }
    target.assign(buffer, static_cast<std::size_t>(n));
    return true;
}

bool readWholeFile(const char* path, std::string& text) {
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::ostringstream out;
    out << in.rdbuf();
    if (in.bad()) {
        return false;
    }
    text = out.str();
    return true;
}

std::vector<std::string> splitTokens(const std::string& line) {
    std::vector<std::string> tokens;
    std::istringstream in(line);
    std::string token;
    while (in >> token) {
        tokens.push_back(token);
    }
    return tokens;
}

bool parseNumber(const std::string& text, int base, unsigned long long& value) {
    if (text.empty() || text.size() > 20) {
        return false;
    }
    for (char c : text) {
        const bool digit = c >= '0' && c <= '9';
        const bool hex = base == 16 && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'));
        if (!digit && !hex) {
            return false;
        }
    }
    errno = 0;
    char* end = nullptr;
    const unsigned long long parsed = std::strtoull(text.c_str(), &end, base);
    if (errno != 0 || end != text.c_str() + text.size()) {
        return false;
    }
    value = parsed;
    return true;
}

bool parseDevIno(const std::string& token, unsigned long long& devMajor,
                 unsigned long long& devMinor, unsigned long long& inode) {
    const std::size_t first = token.find(':');
    if (first == std::string::npos) {
        return false;
    }
    const std::size_t second = token.find(':', first + 1);
    if (second == std::string::npos) {
        return false;
    }
    return parseNumber(token.substr(0, first), 16, devMajor) &&
           parseNumber(token.substr(first + 1, second - first - 1), 16, devMinor) &&
           parseNumber(token.substr(second + 1), 10, inode);
}

bool checkTarget(const struct stat& st, const std::string& name, std::string& error) {
    if (!S_ISREG(st.st_mode)) {
        error = name + " is not a regular file";
        return false;
    }
    if (st.st_nlink != 1) {
        error = name + " has " + std::to_string(static_cast<unsigned long long>(st.st_nlink)) +
                " hard links, refusing to use it";
        return false;
    }
    return true;
}

int openPidfd(pid_t pid) {
#ifdef SYS_pidfd_open
    return static_cast<int>(::syscall(SYS_pidfd_open, static_cast<long>(pid), 0L));
#else
    static_cast<void>(pid);
    return -1;
#endif
}

bool sendTerminate(pid_t pid, int pidfd, int& err) {
#ifdef SYS_pidfd_send_signal
    if (pidfd >= 0) {
        if (::syscall(SYS_pidfd_send_signal, static_cast<long>(pidfd),
                      static_cast<long>(SIGTERM), 0L, 0L) == 0) {
            return true;
        }
        err = errno;
        return false;
    }
#else
    static_cast<void>(pidfd);
#endif
    if (::kill(pid, SIGTERM) == 0) {
        return true;
    }
    err = errno;
    return false;
}

}

PidFile::PidFile(fs::path path) : path_(std::move(path)) {}

PidFile::~PidFile() {
    release();
}

bool PidFile::parsePid(const std::string& text, pid_t& pid) {
    std::size_t end = text.size();
    while (end > 0 && (text[end - 1] == '\n' || text[end - 1] == '\r' ||
                       text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }
    if (end == 0 || end > 10) {
        return false;
    }
    long long value = 0;
    for (std::size_t i = 0; i < end; ++i) {
        if (text[i] < '0' || text[i] > '9') {
            return false;
        }
        value = value * 10 + (text[i] - '0');
    }
    if (value < 2 || value > kMaxPid) {
        return false;
    }
    pid = static_cast<pid_t>(value);
    return true;
}

std::string PidFile::stripDeletedSuffix(std::string text) {
    const std::string suffix = " (deleted)";
    if (text.size() > suffix.size() &&
        text.compare(text.size() - suffix.size(), suffix.size(), suffix) == 0) {
        text.erase(text.size() - suffix.size());
    }
    return text;
}

PidFile::ProcState PidFile::inspect(pid_t pid) {
    if (pid <= 0) {
        return ProcState::Missing;
    }
    const std::string dir = "/proc/" + std::to_string(pid);
    struct stat st;
    if (::stat(dir.c_str(), &st) != 0) {
        return ProcState::Missing;
    }

    std::string line;
    {
        std::ifstream in(dir + "/stat");
        if (!in || !std::getline(in, line)) {
            return ProcState::Missing;
        }
    }
    const std::size_t paren = line.rfind(')');
    if (paren == std::string::npos || paren + 2 >= line.size()) {
        return ProcState::Foreign;
    }
    const char state = line[paren + 2];
    if (state == 'Z' || state == 'X') {
        return ProcState::Zombie;
    }

    std::string theirs;
    std::string mine;
    if (!readLink(dir + "/exe", theirs) || !readLink("/proc/self/exe", mine)) {
        return ProcState::Foreign;
    }
    if (stripDeletedSuffix(theirs) != stripDeletedSuffix(mine)) {
        return ProcState::Foreign;
    }
    if (st.st_uid != ::geteuid()) {
        return ProcState::Foreign;
    }
    return ProcState::Ours;
}

bool PidFile::lockHeldBy(const std::string& locksText, pid_t pid, dev_t dev, ino_t ino) {
    if (pid <= 0) {
        return false;
    }
    std::istringstream lines(locksText);
    std::string line;
    while (std::getline(lines, line)) {
        if (line.find("->") != std::string::npos) {
            continue;
        }
        const std::vector<std::string> tokens = splitTokens(line);
        std::size_t i = 0;
        while (i < tokens.size() && tokens[i] != "FLOCK") {
            ++i;
        }
        if (i + 4 >= tokens.size() || tokens[i + 2] != "WRITE") {
            continue;
        }
        unsigned long long holder = 0;
        unsigned long long devMajor = 0;
        unsigned long long devMinor = 0;
        unsigned long long inode = 0;
        if (!parseNumber(tokens[i + 3], 10, holder) ||
            !parseDevIno(tokens[i + 4], devMajor, devMinor, inode)) {
            continue;
        }
        if (holder == static_cast<unsigned long long>(pid) && devMajor == major(dev) &&
            devMinor == minor(dev) && inode == static_cast<unsigned long long>(ino)) {
            return true;
        }
    }
    return false;
}

PidFile::Previous PidFile::stopPrevious(int timeoutMs) const {
    using Kind = Previous::Kind;
    const std::string name = "PID file '" + path_.string() + "'";

    if (!path_.is_absolute()) {
        return make(Kind::Failed, 0, name + " path is not absolute");
    }

    const int rawFd = ::open(path_.c_str(), O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (rawFd < 0) {
        const int err = errno;
        if (err == ENOENT) {
            return make(Kind::None, 0, "no PID file");
        }
        if (err == ELOOP) {
            return make(Kind::Failed, 0, name + " is a symbolic link, refusing to use it");
        }
        return make(Kind::Failed, 0, "cannot open " + name + ": " + errText(err));
    }
    Fd fd(rawFd);

    struct stat st;
    if (::fstat(fd.get(), &st) != 0) {
        return make(Kind::Failed, 0, "cannot inspect " + name + ": " + errText(errno));
    }
    std::string targetError;
    if (!checkTarget(st, name, targetError)) {
        return make(Kind::Failed, 0, targetError);
    }

    if (::flock(fd.get(), LOCK_SH | LOCK_NB) != 0) {
        const int err = errno;
        if (err != EWOULDBLOCK) {
            return make(Kind::Failed, 0, "cannot test lock on " + name + ": " + errText(err));
        }
    } else {
        ::flock(fd.get(), LOCK_UN);
        pid_t old = 0;
        if (parsePid(readHead(fd.get()), old)) {
            return make(Kind::Stale, old,
                        "stale " + name + ": no running instance holds it (PID " +
                            std::to_string(old) + ")");
        }
        return make(Kind::Stale, 0, "ignoring unlocked " + name + " with invalid content");
    }

    pid_t pid = 0;
    bool valid = false;
    for (int attempt = 0; attempt < kReadRetries && !valid; ++attempt) {
        if (attempt > 0) {
            ::usleep(kReadRetryUs);
        }
        valid = parsePid(readHead(fd.get()), pid);
    }
    if (!valid) {
        return make(Kind::Failed, 0,
                    name + " is locked by a running process but does not contain a valid PID; "
                           "refusing to continue");
    }
    if (pid == ::getpid()) {
        return make(Kind::Failed, pid,
                    name + " is locked and names this very process; refusing to continue");
    }

    const std::string who = "process " + std::to_string(pid);
    Fd pidfd(openPidfd(pid));
    const ProcState state = inspect(pid);
    if (state == ProcState::Missing || state == ProcState::Zombie) {
        return make(Kind::Failed, pid,
                    name + " is locked, but " + who +
                        " is not running; cannot identify the lock holder");
    }
    if (state == ProcState::Foreign) {
        return make(Kind::Failed, pid,
                    name + " is locked, but " + who +
                        " is not an instance of this program; no signal sent");
    }

    std::string locks;
    if (!readWholeFile("/proc/locks", locks)) {
        return make(Kind::Failed, pid,
                    "cannot read /proc/locks to confirm who holds the lock on " + name +
                        "; no signal sent");
    }
    if (!lockHeldBy(locks, pid, st.st_dev, st.st_ino)) {
        return make(Kind::Failed, pid,
                    name + " is locked, but the lock is not held by " + who +
                        "; no signal sent");
    }

    int sendErr = 0;
    if (!sendTerminate(pid, pidfd.get(), sendErr) && sendErr != ESRCH) {
        return make(Kind::Failed, pid, "cannot send SIGTERM to " + who + ": " + errText(sendErr));
    }

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    for (;;) {
        if (inspect(pid) != ProcState::Ours) {
            return make(Kind::Stopped, pid,
                        "previous instance (PID " + std::to_string(pid) +
                            ") terminated after SIGTERM");
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            return make(Kind::Failed, pid,
                        "previous instance (PID " + std::to_string(pid) +
                            ") did not exit within " + std::to_string(timeoutMs) +
                            " ms; SIGKILL is not sent");
        }
        ::usleep(kPollUs);
    }
}

bool PidFile::acquire(std::string& error) {
    const std::string name = "PID file '" + path_.string() + "'";
    if (fd_ >= 0) {
        error = name + " is already held by this object";
        return false;
    }
    if (!path_.is_absolute()) {
        error = name + " path is not absolute";
        return false;
    }

    const std::string text = std::to_string(::getpid()) + "\n";
    for (int attempt = 0; attempt < 5; ++attempt) {
        const int rawFd = ::open(path_.c_str(), O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0644);
        const int openErr = errno;
        Fd fd(rawFd);
        if (fd.get() < 0) {
            error = openErr == ELOOP ? name + " is a symbolic link, refusing to use it"
                                     : "cannot open " + name + ": " + errText(openErr);
            return false;
        }

        struct stat st;
        if (::fstat(fd.get(), &st) != 0) {
            error = "cannot inspect " + name + ": " + errText(errno);
            return false;
        }
        if (!checkTarget(st, name, error)) {
            return false;
        }
        if (st.st_uid != ::geteuid()) {
            error = name + " belongs to another user";
            return false;
        }

        if (::flock(fd.get(), LOCK_EX | LOCK_NB) != 0) {
            const int err = errno;
            if (err == EWOULDBLOCK) {
                error = "another instance is already running (" + name + " is locked)";
            } else {
                error = "cannot lock " + name + ": " + errText(err);
            }
            return false;
        }

        struct stat locked;
        if (::fstat(fd.get(), &locked) != 0) {
            error = "cannot inspect " + name + ": " + errText(errno);
            return false;
        }
        if (!checkTarget(locked, name, error)) {
            return false;
        }

        struct stat onDisk;
        if (::lstat(path_.c_str(), &onDisk) != 0 || onDisk.st_ino != locked.st_ino ||
            onDisk.st_dev != locked.st_dev) {
            continue;
        }

        if (::ftruncate(fd.get(), 0) != 0) {
            error = "cannot truncate " + name + ": " + errText(errno);
            return false;
        }
        const ssize_t written = ::write(fd.get(), text.data(), text.size());
        if (written != static_cast<ssize_t>(text.size())) {
            error = "cannot write " + name + ": " + errText(errno);
            return false;
        }

        fd_ = fd.release();
        owner_ = ::getpid();
        return true;
    }
    error = name + " keeps being replaced, giving up";
    return false;
}

void PidFile::release() {
    if (fd_ < 0) {
        return;
    }
    const int fd = fd_;
    fd_ = -1;

    if (owner_ == ::getpid()) {
        const std::string expected = std::to_string(::getpid()) + "\n";
        struct stat mine;
        struct stat onDisk;
        if (readHead(fd) == expected && ::fstat(fd, &mine) == 0 &&
            ::lstat(path_.c_str(), &onDisk) == 0 && mine.st_ino == onDisk.st_ino &&
            mine.st_dev == onDisk.st_dev) {
            static_cast<void>(::unlink(path_.c_str()));
        }
    }
    ::close(fd);
}
