#include "disk_monitor/ProcessControl.hpp"
#include "disk_monitor/Logging.hpp"

#include <array>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <syslog.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

namespace diskmon {
namespace {

constexpr auto kLockWaitLimit = std::chrono::seconds(10);
constexpr auto kPollInterval = std::chrono::milliseconds(100);
constexpr auto kOldProcessWaitLimit = std::chrono::seconds(5);

bool writeAll(int descriptor, const char* data, std::size_t size) {
    std::size_t written = 0;
    while (written < size) {
        const ssize_t amount = ::write(descriptor, data + written, size - written);
        if (amount < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        if (amount == 0) {
            errno = EIO;
            return false;
        }
        written += static_cast<std::size_t>(amount);
    }
    return true;
}

enum class PidReadStatus {
    Missing,
    Valid,
    InvalidContents,
    UnsafePath,
    Error
};

struct PidReadResult {
    PidReadStatus status = PidReadStatus::Missing;
    pid_t pid = -1;
    dev_t device = 0;
    ino_t inode = 0;
    std::string message;
};

PidReadResult readPid(const std::filesystem::path& path) {
    const int descriptor = ::open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (descriptor < 0) {
        if (errno == ENOENT) {
            return {PidReadStatus::Missing, -1, 0, 0, {}};
        }
        if (errno == ELOOP) {
            return {PidReadStatus::UnsafePath, -1, 0, 0,
                    "PID path must not be a symbolic link: '" + path.string() + "'"};
        }
        return {PidReadStatus::Error, -1, 0, 0,
                "cannot open PID file '" + path.string() + "': " + std::strerror(errno)};
    }

    struct stat metadata {};
    if (::fstat(descriptor, &metadata) != 0) {
        const int savedError = errno;
        (void)::close(descriptor);
        return {PidReadStatus::Error, -1, 0, 0,
                "cannot inspect PID file '" + path.string() + "': " + std::strerror(savedError)};
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_nlink != 1) {
        (void)::close(descriptor);
        return {PidReadStatus::UnsafePath, -1, metadata.st_dev, metadata.st_ino,
                "PID path must be a regular, singly-linked file: '" + path.string() + "'"};
    }

    std::array<char, 129> buffer{};
    std::size_t total = 0;
    bool readFailed = false;
    int readError = 0;
    while (total < buffer.size()) {
        const ssize_t amount = ::read(descriptor, buffer.data() + total, buffer.size() - total);
        if (amount > 0) {
            total += static_cast<std::size_t>(amount);
            continue;
        }
        if (amount == 0) {
            break;
        }
        if (errno == EINTR) {
            continue;
        }
        readFailed = true;
        readError = errno;
        break;
    }
    (void)::close(descriptor);

    struct stat currentPathMetadata {};
    if (::lstat(path.c_str(), &currentPathMetadata) != 0 ||
        currentPathMetadata.st_dev != metadata.st_dev || currentPathMetadata.st_ino != metadata.st_ino ||
        !S_ISREG(currentPathMetadata.st_mode) || currentPathMetadata.st_nlink != 1) {
        return {PidReadStatus::UnsafePath, -1, metadata.st_dev, metadata.st_ino,
                "PID path changed or became unsafe while being read: '" + path.string() + "'"};
    }
    if (readFailed) {
        return {PidReadStatus::Error, -1, metadata.st_dev, metadata.st_ino,
                "cannot read PID file '" + path.string() + "': " + std::strerror(readError)};
    }
    if (total == buffer.size()) {
        return {PidReadStatus::InvalidContents, -1, metadata.st_dev, metadata.st_ino,
                "PID file is too large: '" + path.string() + "'"};
    }

    std::istringstream input(std::string(buffer.data(), total));
    long long value = 0;
    if (!(input >> value) || value <= 1 ||
        value > static_cast<long long>(std::numeric_limits<pid_t>::max())) {
        return {PidReadStatus::InvalidContents, -1, metadata.st_dev, metadata.st_ino,
                "PID file has invalid contents: '" + path.string() + "'"};
    }
    input >> std::ws;
    if (!input.eof()) {
        return {PidReadStatus::InvalidContents, -1, metadata.st_dev, metadata.st_ino,
                "PID file has trailing data: '" + path.string() + "'"};
    }
    return {PidReadStatus::Valid, static_cast<pid_t>(value), metadata.st_dev, metadata.st_ino, {}};
}

bool pidPathStillMatches(const std::filesystem::path& path, const PidReadResult& result) {
    if (result.status != PidReadStatus::Valid) {
        return false;
    }
    struct stat metadata {};
    return ::lstat(path.c_str(), &metadata) == 0 && S_ISREG(metadata.st_mode) &&
           metadata.st_nlink == 1 && metadata.st_dev == result.device && metadata.st_ino == result.inode;
}

bool rejectUnsafePidRead(const PidReadResult& result, std::string& error) {
    if (result.status != PidReadStatus::UnsafePath && result.status != PidReadStatus::Error) {
        return false;
    }
    error = result.message;
    return true;
}

bool processExists(pid_t pid) {
    if (pid <= 1) {
        return false;
    }
    if (::kill(pid, 0) == 0) {
        return true;
    }
    return errno == EPERM;
}

bool waitUntilProcessGone(pid_t pid) {
    const auto deadline = std::chrono::steady_clock::now() + kOldProcessWaitLimit;
    while (std::chrono::steady_clock::now() < deadline) {
        if (!processExists(pid) || !processUsesCurrentExecutable(pid)) {
            return true;
        }
        std::this_thread::sleep_for(kPollInterval);
    }
    return !processExists(pid) || !processUsesCurrentExecutable(pid);
}

bool writeStatus(int descriptor, char value) {
    return descriptor >= 0 && writeAll(descriptor, &value, sizeof(value));
}

std::string executableLinkTarget(const std::string& procPath) {
    std::array<char, 4096> buffer{};
    const ssize_t length = ::readlink(procPath.c_str(), buffer.data(), buffer.size() - 1U);
    if (length <= 0 || static_cast<std::size_t>(length) >= buffer.size() - 1U) {
        return {};
    }
    return std::string(buffer.data(), static_cast<std::size_t>(length));
}

std::string removeDeletedSuffix(std::string path) {
    constexpr const char* suffix = " (deleted)";
    constexpr std::size_t suffixLength = 10U;
    if (path.size() >= suffixLength && path.compare(path.size() - suffixLength, suffixLength, suffix) == 0) {
        path.erase(path.size() - suffixLength);
    }
    return path;
}

}  // namespace

ProcessLock::~ProcessLock() { closeDescriptor(); }

bool ProcessLock::acquire(const std::filesystem::path& pidPath, std::string& error) {
    const std::filesystem::path lockPath(pidPath.string() + ".lock");
    lockDescriptor_ = ::open(lockPath.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0644);
    if (lockDescriptor_ < 0) {
        error = "cannot open lock file '" + lockPath.string() + "': " + std::strerror(errno);
        return false;
    }
    struct stat lockMetadata {};
    if (::fstat(lockDescriptor_, &lockMetadata) != 0) {
        error = "cannot inspect lock file '" + lockPath.string() + "': " + std::strerror(errno);
        closeDescriptor();
        return false;
    }
    if (!S_ISREG(lockMetadata.st_mode) || lockMetadata.st_nlink != 1) {
        error = "lock path must be a regular, singly-linked file: '" + lockPath.string() + "'";
        closeDescriptor();
        return false;
    }

    if (::flock(lockDescriptor_, LOCK_EX | LOCK_NB) == 0) {
        const PidReadResult previous = readPid(pidPath);
        if (rejectUnsafePidRead(previous, error)) {
            closeDescriptor();
            return false;
        }
        if (previous.status == PidReadStatus::Valid && previous.pid != ::getpid() &&
            processExists(previous.pid) && processUsesCurrentExecutable(previous.pid)) {
            if (!pidPathStillMatches(pidPath, previous)) {
                error = "PID file changed before the previous-process check; refusing to signal any process";
                closeDescriptor();
                return false;
            }
            if (::kill(previous.pid, SIGTERM) != 0 && errno != ESRCH) {
                error = "cannot send SIGTERM to verified previous disk-monitor process " +
                        std::to_string(previous.pid) + ": " + std::strerror(errno);
                closeDescriptor();
                return false;
            }
            if (!waitUntilProcessGone(previous.pid)) {
                error = "verified previous disk-monitor process " + std::to_string(previous.pid) +
                        " did not terminate within the timeout";
                closeDescriptor();
                return false;
            }
        }
        return true;
    }
    if (errno != EWOULDBLOCK && errno != EAGAIN) {
        error = "cannot lock '" + lockPath.string() + "': " + std::strerror(errno);
        closeDescriptor();
        return false;
    }

    bool signalSent = false;
    pid_t signaledPid = -1;
    const auto deadline = std::chrono::steady_clock::now() + kLockWaitLimit;
    while (std::chrono::steady_clock::now() < deadline) {
        const PidReadResult active = readPid(pidPath);
        if (rejectUnsafePidRead(active, error)) {
            closeDescriptor();
            return false;
        }
        if (active.status == PidReadStatus::Valid && active.pid > 1 &&
            active.pid != ::getpid() && !signalSent && processExists(active.pid) &&
            processUsesCurrentExecutable(active.pid)) {
            if (!pidPathStillMatches(pidPath, active)) {
                error = "PID file changed before the previous-process check; refusing to signal any process";
                closeDescriptor();
                return false;
            }
            if (::kill(active.pid, SIGTERM) == 0 || errno == ESRCH) {
                signalSent = true;
                signaledPid = active.pid;
            } else {
                error = "cannot send SIGTERM to verified previous disk-monitor process " +
                        std::to_string(active.pid) + ": " + std::strerror(errno);
                closeDescriptor();
                return false;
            }
        }

        if (::flock(lockDescriptor_, LOCK_EX | LOCK_NB) == 0) {
            if (signaledPid > 1 && processExists(signaledPid) &&
                processUsesCurrentExecutable(signaledPid) && !waitUntilProcessGone(signaledPid)) {
                error = "previous disk-monitor process did not finish after releasing its lock";
                closeDescriptor();
                return false;
            }
            return true;
        }
        if (errno != EWOULDBLOCK && errno != EAGAIN && errno != EINTR) {
            error = "cannot acquire lock '" + lockPath.string() + "': " + std::strerror(errno);
            closeDescriptor();
            return false;
        }
        std::this_thread::sleep_for(kPollInterval);
    }

    const PidReadResult active = readPid(pidPath);
    if (rejectUnsafePidRead(active, error)) {
        closeDescriptor();
        return false;
    }
    if (active.status == PidReadStatus::Valid && processExists(active.pid) &&
        !processUsesCurrentExecutable(active.pid)) {
        error = "lock is held, but PID " + std::to_string(active.pid) +
                " does not identify this executable; refusing to signal it";
    } else if (active.status == PidReadStatus::InvalidContents) {
        error = active.message + "; refusing to signal any process";
    } else {
        error = "timed out waiting for the existing disk-monitor instance to stop";
    }
    closeDescriptor();
    return false;
}

void ProcessLock::closeDescriptor() {
    if (lockDescriptor_ >= 0) {
        (void)::close(lockDescriptor_);
        lockDescriptor_ = -1;
    }
}

int ProcessLock::descriptor() const noexcept { return lockDescriptor_; }

PidFile::PidFile(std::filesystem::path path) : path_(std::move(path)) {}

PidFile::~PidFile() {
    std::string ignoredError;
    (void)removeIfOwned(ignoredError);
}

bool PidFile::writeCurrentPid(std::string& error) {
    const int descriptor = ::open(path_.c_str(), O_WRONLY | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0644);
    if (descriptor < 0) {
        error = "cannot open PID file '" + path_.string() + "': " + std::strerror(errno);
        return false;
    }

    struct stat metadata {};
    if (::fstat(descriptor, &metadata) != 0) {
        error = "cannot inspect PID file '" + path_.string() + "': " + std::strerror(errno);
        (void)::close(descriptor);
        return false;
    }
    if (!S_ISREG(metadata.st_mode) || metadata.st_nlink != 1) {
        error = "PID path must be a regular, singly-linked file: '" + path_.string() + "'";
        (void)::close(descriptor);
        return false;
    }
    if (::ftruncate(descriptor, 0) != 0) {
        error = "cannot truncate PID file '" + path_.string() + "': " + std::strerror(errno);
        (void)::close(descriptor);
        return false;
    }
    const std::string contents = std::to_string(::getpid()) + "\n";
    const bool written = writeAll(descriptor, contents.data(), contents.size());
    const int writeError = errno;
    const bool synced = written && ::fsync(descriptor) == 0;
    const int syncError = errno;
    const bool closed = ::close(descriptor) == 0;

    if (!written || !synced || !closed) {
        const int errorCode = !written ? writeError : (!synced ? syncError : errno);
        struct stat pathMetadata {};
        if (::lstat(path_.c_str(), &pathMetadata) == 0 &&
            pathMetadata.st_dev == metadata.st_dev && pathMetadata.st_ino == metadata.st_ino) {
            (void)::unlink(path_.c_str());
        }
        error = "cannot write PID file '" + path_.string() + "': " + std::strerror(errorCode);
        return false;
    }

    device_ = metadata.st_dev;
    inode_ = metadata.st_ino;
    ownerPid_ = ::getpid();
    created_ = true;
    return true;
}

bool PidFile::removeIfOwned(std::string& error) noexcept {
    if (!created_) {  return true; }
    struct stat metadata {};
    if (::lstat(path_.c_str(), &metadata) != 0) {
        const int savedError = errno;
        created_ = false;
        if (savedError == ENOENT) {
            return true;
        }
        error = "cannot inspect PID file during cleanup '" + path_.string() + "': " + std::strerror(savedError);
        return false;
    }
    if (metadata.st_dev != device_ || metadata.st_ino != inode_) {
        created_ = false;
        return true;
    }
    const PidReadResult filePid = readPid(path_);
    if (filePid.status != PidReadStatus::Valid || filePid.pid != ownerPid_ ||
        filePid.device != device_ || filePid.inode != inode_ || !pidPathStillMatches(path_, filePid)) {
        created_ = false;
        return true;
    }
    struct stat finalMetadata {};
    if (::lstat(path_.c_str(), &finalMetadata) != 0 || finalMetadata.st_dev != device_ ||
        finalMetadata.st_ino != inode_ || !S_ISREG(finalMetadata.st_mode) || finalMetadata.st_nlink != 1) {
        created_ = false;
        return true;
    }
    if (::unlink(path_.c_str()) != 0 && errno != ENOENT) {
        const int savedError = errno;
        created_ = false;
        error = "cannot remove owned PID file '" + path_.string() + "': " + std::strerror(savedError);
        return false;
    }
    created_ = false;
    return true;
}

const std::filesystem::path& PidFile::path() const noexcept { return path_; }

DaemonizeResult daemonizeProcess() {
    int pipes[2] = {-1, -1};
    if (::pipe2(pipes, O_CLOEXEC) != 0) {
        const std::string message = "pipe2 failed during daemonization: " + std::string(std::strerror(errno));
        return {DaemonizeResult::Role::Error, -1, message};
    }

    const pid_t firstChild = ::fork();
    if (firstChild < 0) {
        const std::string message = "fork failed during daemonization: " + std::string(std::strerror(errno));
        (void)::close(pipes[0]);
        (void)::close(pipes[1]);
        return {DaemonizeResult::Role::Error, -1, message};
    }
    if (firstChild > 0) {
        (void)::close(pipes[1]);
        return {DaemonizeResult::Role::Parent, pipes[0], {}, firstChild};
    }

    (void)::close(pipes[0]);
    if (::setsid() < 0) {
        logDaemon(LOG_ERR, "setsid failed during daemonization: " + std::string(std::strerror(errno)));
        (void)writeStatus(pipes[1], 'E');
        (void)::close(pipes[1]);
        ::_exit(1);
    }

    const pid_t daemonChild = ::fork();
    if (daemonChild < 0) {
        logDaemon(LOG_ERR, "second fork failed during daemonization: " + std::string(std::strerror(errno)));
        (void)writeStatus(pipes[1], 'E');
        (void)::close(pipes[1]);
        ::_exit(1);
    }
    if (daemonChild > 0) {
        (void)::close(pipes[1]);
        ::_exit(0);
    }

    if (::chdir("/") != 0) {
        logDaemon(LOG_ERR, "chdir('/') failed during daemonization: " + std::string(std::strerror(errno)));
        (void)writeStatus(pipes[1], 'E');
        (void)::close(pipes[1]);
        ::_exit(1);
    }
    (void)::umask(027);

    const int nullDescriptor = ::open("/dev/null", O_RDWR | O_CLOEXEC);
    if (nullDescriptor < 0) {
        logDaemon(LOG_ERR, "opening /dev/null failed during daemonization: " + std::string(std::strerror(errno)));
        (void)writeStatus(pipes[1], 'E');
        (void)::close(pipes[1]);
        ::_exit(1);
    }
    for (int standardDescriptor = STDIN_FILENO; standardDescriptor <= STDERR_FILENO; ++standardDescriptor) {
        if (::dup2(nullDescriptor, standardDescriptor) < 0) {
            logDaemon(LOG_ERR, "dup2 failed during daemonization: " + std::string(std::strerror(errno)));
            (void)::close(nullDescriptor);
            (void)writeStatus(pipes[1], 'E');
            (void)::close(pipes[1]);
            ::_exit(1);
        }
    }
    if (nullDescriptor > STDERR_FILENO) { (void)::close(nullDescriptor);}
    return {DaemonizeResult::Role::Child, pipes[1], {}};
}

bool reportDaemonStartup(int statusPipe, bool success) {
    const char status = success ? 'O' : 'E';
    const bool result = writeStatus(statusPipe, status);
    if (statusPipe >= 0) {
        (void)::close(statusPipe);
    }
    return result;
}

bool waitForDaemonStartup(int statusPipe, std::string& error) {
    char status = 'E';
    ssize_t amount = -1;
    do {
        amount = ::read(statusPipe, &status, sizeof(status));
    } while (amount < 0 && errno == EINTR);
    const int readError = errno;
    (void)::close(statusPipe);
    if (amount == 1 && status == 'O') {
        return true;
    }
    if (amount < 0) {
        error = "failed while waiting for daemon startup: " + std::string(std::strerror(readError));
    } else {
        error = "daemon failed to initialize; inspect the system journal for details";
    }
    return false;
}

bool reapDaemonizationChild(pid_t childPid, std::string& error) {
    if (childPid <= 0) {
        return true;
    }
    int childStatus = 0;
    pid_t result = -1;
    do {
        result = ::waitpid(childPid, &childStatus, 0);
    } while (result < 0 && errno == EINTR);
    if (result < 0) {
        error = "waitpid failed for daemonization child " + std::to_string(childPid) +
                ": " + std::strerror(errno);
        return false;
    }
    return true;
}
bool processUsesCurrentExecutable(pid_t pid) {
    if (pid <= 1) { return false; }
    struct stat currentMetadata {};
    if (::stat("/proc/self/exe", &currentMetadata) != 0) { return false; }
    const std::string target = "/proc/" + std::to_string(pid) + "/exe";
    struct stat processMetadata {};
    if (::stat(target.c_str(), &processMetadata) != 0) { return false; }
    if (currentMetadata.st_dev == processMetadata.st_dev && currentMetadata.st_ino == processMetadata.st_ino) {
        return true;
    }
    const std::string currentPath = executableLinkTarget("/proc/self/exe");
    const std::string processPath = executableLinkTarget("/proc/" + std::to_string(pid) + "/exe");
    return !currentPath.empty() && !processPath.empty() &&
           currentPath == removeDeletedSuffix(processPath);
}
}  // namespace diskmon
