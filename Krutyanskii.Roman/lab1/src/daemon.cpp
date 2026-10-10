#include "daemon.h"

#include <fcntl.h>
#include <signal.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "fileops.h"
#include "logger.h"

namespace lab1 {
namespace {

/// Number of times the start of a new instance waits for the old one to die.
constexpr int kStopAttempts = 50;

/// Pause between two checks of the previous instance, in milliseconds.
constexpr long kStopRetryDelayMs = 100;

/// Set by the signal handlers, read by the main loop.
volatile sig_atomic_t g_reloadRequested = 0;
volatile sig_atomic_t g_terminationRequested = 0;

extern "C" void handleTermination(int /*signalNumber*/) {
    g_terminationRequested = 1;
}

extern "C" void handleReload(int /*signalNumber*/) {
    g_reloadRequested = 1;
}

void sleepMilliseconds(long milliseconds) {
    struct timespec request {};
    request.tv_sec = milliseconds / 1000;
    request.tv_nsec = (milliseconds % 1000) * 1000000L;

    struct timespec remaining {};
    while (nanosleep(&request, &remaining) == -1 && errno == EINTR) {
        request = remaining;
    }
}

/// Reads the target of a symbolic link, returns an empty string on failure.
std::string readLink(const std::string& path) {
    std::vector<char> buffer(4096);
    const ssize_t length = ::readlink(path.c_str(), buffer.data(), buffer.size() - 1);
    if (length <= 0) {
        return std::string();
    }
    return std::string(buffer.data(), static_cast<std::size_t>(length));
}

/// Reads a whole regular file into a string. Returns false for anything that
/// cannot be read as a regular file, including a directory: reading a directory
/// through std::ifstream throws, and an escaped exception would abort the
/// daemon while it is only reading its own pid file.
bool readFile(const std::string& path, std::string& content) {
    struct stat info {};
    if (lstat(path.c_str(), &info) == -1 || !S_ISREG(info.st_mode)) {
        return false;
    }

    try {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }
        content.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    } catch (const std::exception& error) {
        log::error("lab1: cannot read %s: %s", path.c_str(), error.what());
        return false;
    }
    return true;
}

/// Sends the startup verdict back to the process that forked us, so that a
/// failure after the fork is not reported as a successful start.
void reportStartup(int statusFd, bool success) {
    if (statusFd < 0) {
        return;
    }
    const char verdict = success ? 0 : 1;
    ssize_t written = 0;
    while ((written = write(statusFd, &verdict, 1)) == -1 && errno == EINTR) {
    }
    close(statusFd);
}

/// Waits for the verdict of the daemonized child. A closed pipe means the child
/// died before it could report, which is a failure as well.
bool readStartupVerdict(int statusFd) {
    char verdict = 1;
    ssize_t received = 0;
    while ((received = read(statusFd, &verdict, 1)) == -1 && errno == EINTR) {
    }
    close(statusFd);
    return received == 1 && verdict == 0;
}

/// Closes every descriptor above the three standard streams, so that nothing
/// inherited from the launching shell stays alive inside the daemon. The
/// descriptor used to report the startup verdict is kept open on purpose.
void closeInheritedDescriptors(int keepFd) {
    // A plain loop is used instead of closefrom() because closefrom() has no
    // way to spare a single descriptor, and the startup verdict pipe needs one.
    constexpr long kDescriptorCeiling = 4096;
    const long maxDescriptor = sysconf(_SC_OPEN_MAX);
    const long limit = (maxDescriptor < 0) ? kDescriptorCeiling
                                           : std::min(maxDescriptor, kDescriptorCeiling);

    for (long descriptor = STDERR_FILENO + 1; descriptor < limit; ++descriptor) {
        if (descriptor != keepFd) {
            close(static_cast<int>(descriptor));
        }
    }
}

}  // namespace

Daemon& Daemon::instance() {
    static Daemon theDaemon;
    return theDaemon;
}

RunResult Daemon::run(const std::string& configPath) {
    executablePath_ = readLink("/proc/self/exe");

    // The config file is looked up relative to the working directory of the
    // caller, so its absolute path has to be resolved before chdir("/").
    if (!resolveConfigPath(configPath)) {
        return RunResult::kConfigNotFound;
    }

    if (!config_.load(configPath_)) {
        return RunResult::kConfigInvalid;
    }

    if (!stopPreviousInstance()) {
        return RunResult::kPreviousInstanceRunning;
    }

    if (!daemonize()) {
        reportStartup(startupStatusFd_, false);
        return RunResult::kDaemonizationFailed;
    }

    // The journal connection was opened before the fork and its descriptor has
    // just been closed together with the inherited ones, so it is re-established.
    log::close();
    log::open(kLogIdent);

    // The handlers are installed before the pid file is published, so that even a
    // signal arriving right now ends the daemon through the normal cleanup.
    installSignalHandlers();

    if (!writePidFile()) {
        reportStartup(startupStatusFd_, false);
        return RunResult::kPidFileFailed;
    }

    reportStartup(startupStatusFd_, true);
    startupStatusFd_ = -1;

    log::info("lab1: daemon started with pid %d, config %s, pid file %s",
              static_cast<int>(pid_), configPath_.c_str(), kPidFilePath);

    mainLoop();

    log::info("lab1: daemon is shutting down, removing pid file");
    removePidFile();
    return RunResult::kSuccess;
}

bool Daemon::resolveConfigPath(const std::string& configPath) {
    char* resolved = realpath(configPath.c_str(), nullptr);
    if (resolved == nullptr) {
        log::error("lab1: cannot resolve config file %s: %s", configPath.c_str(),
                   std::strerror(errno));
        return false;
    }
    configPath_ = resolved;
    std::free(resolved);
    return true;
}

bool Daemon::stopPreviousInstance() {
    if (!fileops::exists(kPidFilePath)) {
        return true;
    }

    std::string content;
    if (!readFile(kPidFilePath, content)) {
        log::error("lab1: pid file %s is not a readable regular file, ignoring it",
                   kPidFilePath);
        return true;
    }

    char* end = nullptr;
    const long storedPid = std::strtol(content.c_str(), &end, 10);
    if (end == content.c_str() || storedPid <= 1) {
        log::error("lab1: pid file %s is corrupted, ignoring it", kPidFilePath);
        return true;
    }

    const pid_t previous = static_cast<pid_t>(storedPid);
    const std::string procDir = "/proc/" + std::to_string(storedPid);

    if (!fileops::exists(procDir)) {
        log::info("lab1: pid file is stale, process %ld is gone", storedPid);
        return true;
    }

    // The pid may have been reused by an unrelated process, so the executable
    // behind it is compared with ours before anything is sent to it.
    if (!executablePath_.empty() && readLink(procDir + "/exe") != executablePath_) {
        log::error("lab1: pid %ld belongs to another program, ignoring the pid file",
                   storedPid);
        return true;
    }

    log::info("lab1: another instance runs with pid %ld, sending SIGTERM", storedPid);
    if (kill(previous, SIGTERM) == -1) {
        log::error("lab1: cannot send SIGTERM to pid %ld: %s", storedPid,
                   std::strerror(errno));
        return false;
    }

    for (int attempt = 0; attempt < kStopAttempts; ++attempt) {
        if (!fileops::exists(procDir)) {
            log::info("lab1: previous instance with pid %ld has stopped", storedPid);
            return true;
        }
        sleepMilliseconds(kStopRetryDelayMs);
    }

    log::error("lab1: previous instance with pid %ld did not stop in %d ms", storedPid,
               kStopAttempts * static_cast<int>(kStopRetryDelayMs));
    return false;
}

bool Daemon::daemonize() {
    int statusPipe[2];
    if (pipe(statusPipe) == -1) {
        log::error("lab1: pipe failed: %s", std::strerror(errno));
        return false;
    }

    const pid_t child = fork();
    if (child == -1) {
        log::error("lab1: fork failed: %s", std::strerror(errno));
        close(statusPipe[0]);
        close(statusPipe[1]);
        return false;
    }
    if (child > 0) {
        // The original process leaves as soon as the child reports whether it
        // managed to start, so that a startup failure is visible to the caller.
        close(statusPipe[1]);
        _exit(readStartupVerdict(statusPipe[0]) ? EXIT_SUCCESS : EXIT_FAILURE);
    }

    close(statusPipe[0]);
    startupStatusFd_ = statusPipe[1];

    // A single fork is enough: the daemon becomes the leading process of its own
    // session, exactly as the statement asks, and SIGHUP makes it reload the
    // config instead of terminating, so the hangup that follows the exit of the
    // launching shell does not kill it.
    if (setsid() == -1) {
        log::error("lab1: setsid failed: %s", std::strerror(errno));
        return false;
    }

    if (chdir("/") == -1) {
        log::error("lab1: chdir(\"/\") failed: %s", std::strerror(errno));
        return false;
    }
    umask(0);

    const int nullFd = open("/dev/null", O_RDWR);
    if (nullFd == -1) {
        log::error("lab1: cannot open /dev/null: %s", std::strerror(errno));
        return false;
    }
    if (dup2(nullFd, STDIN_FILENO) == -1 || dup2(nullFd, STDOUT_FILENO) == -1 ||
        dup2(nullFd, STDERR_FILENO) == -1) {
        log::error("lab1: cannot redirect the standard streams: %s", std::strerror(errno));
        return false;
    }
    if (nullFd > STDERR_FILENO) {
        close(nullFd);
    }
    closeInheritedDescriptors(startupStatusFd_);

    pid_ = getpid();
    return true;
}

bool Daemon::writePidFile() {
    // O_NOFOLLOW matters here: the pid file lives in a world writable
    // directory, so fopen() would happily follow a symlink planted by someone
    // else and truncate the file it points at.
    const int fd = open(kPidFilePath, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC,
                        S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd == -1) {
        log::error("lab1: cannot create pid file %s: %s", kPidFilePath, std::strerror(errno));
        return false;
    }

    const std::string content = std::to_string(static_cast<int>(pid_)) + '\n';
    const ssize_t written = write(fd, content.data(), content.size());
    const int writeError = errno;
    if (close(fd) == -1 || written != static_cast<ssize_t>(content.size())) {
        log::error("lab1: cannot write pid file %s: %s", kPidFilePath,
                   std::strerror(written < 0 ? writeError : errno));
        return false;
    }
    return true;
}

void Daemon::removePidFile() {
    std::string content;
    if (readFile(kPidFilePath, content) && !content.empty()) {
        const long storedPid = std::strtol(content.c_str(), nullptr, 10);
        if (storedPid != static_cast<long>(pid_)) {
            // Another instance has taken the pid file over, leave it alone.
            return;
        }
    }
    if (unlink(kPidFilePath) == -1 && errno != ENOENT) {
        log::error("lab1: cannot remove pid file %s: %s", kPidFilePath, std::strerror(errno));
    }
}

void Daemon::installSignalHandlers() {
    struct sigaction action {};
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    action.sa_handler = handleTermination;
    if (sigaction(SIGTERM, &action, nullptr) == -1) {
        log::error("lab1: cannot handle SIGTERM: %s", std::strerror(errno));
    }
    if (sigaction(SIGINT, &action, nullptr) == -1) {
        log::error("lab1: cannot handle SIGINT: %s", std::strerror(errno));
    }

    action.sa_handler = handleReload;
    if (sigaction(SIGHUP, &action, nullptr) == -1) {
        log::error("lab1: cannot handle SIGHUP: %s", std::strerror(errno));
    }

    // A broken standard stream must not kill the daemon.
    struct sigaction ignored {};
    sigemptyset(&ignored.sa_mask);
    ignored.sa_flags = 0;
    ignored.sa_handler = SIG_IGN;
    if (sigaction(SIGPIPE, &ignored, nullptr) == -1) {
        log::error("lab1: cannot ignore SIGPIPE: %s", std::strerror(errno));
    }
}

void Daemon::mainLoop() {
    // An empty mask leaves every signal unblocked, so that the handlers above are
    // actually reached while pselect() waits.
    sigset_t unblockAll;
    sigemptyset(&unblockAll);

    while (g_terminationRequested == 0) {
        struct timespec timeout {};
        timeout.tv_sec = config_.tickSeconds();

        const int ready = pselect(0, nullptr, nullptr, nullptr, &timeout, &unblockAll);

        if (g_terminationRequested != 0) {
            break;
        }
        if (g_reloadRequested != 0) {
            g_reloadRequested = 0;
            reloadConfig();
            continue;
        }
        if (ready == -1) {
            if (errno == EINTR) {
                continue;
            }
            log::error("lab1: pselect failed: %s", std::strerror(errno));
            break;
        }

        performActions();
    }
}

void Daemon::reloadConfig() {
    log::info("lab1: SIGHUP received, reloading config file %s", configPath_.c_str());
    if (!config_.load(configPath_)) {
        log::error("lab1: reload failed, the previous config stays active");
    }
}

void Daemon::performActions() {
    const std::vector<Rule>& rules = config_.rules();
    if (rules.empty()) {
        log::info("lab1: no rules configured, nothing to do");
        return;
    }

    // Rules carry their own interval, so the loop ticks on the shortest one and
    // each rule is processed only once its own interval has passed.
    const Clock::time_point now = Clock::now();
    if (nextRun_.size() != rules.size()) {
        nextRun_.assign(rules.size(), Clock::time_point());
    }

    int processed = 0;
    for (std::size_t index = 0; index < rules.size(); ++index) {
        if (nextRun_[index] > now) {
            continue;
        }
        processRule(rules[index]);
        nextRun_[index] = now + std::chrono::seconds(rules[index].intervalSeconds);
        ++processed;
    }

    log::info("lab1: processed %d of %zu rule(s)", processed, rules.size());
}

void Daemon::processRule(const Rule& rule) const {
    const std::string guard = joinPath(rule.folder, rule.ignoreFile);

    if (fileops::exists(guard)) {
        log::info("lab1: %s found in %s, the folder is left untouched",
                  rule.ignoreFile.c_str(), rule.folder.c_str());
        return;
    }

    if (fileops::isSymlink(rule.folder)) {
        log::info("lab1: %s is a symlink, the folder it points at is cleared",
                  rule.folder.c_str());
    }

    log::info("lab1: %s not found in %s, clearing the folder",
              rule.ignoreFile.c_str(), rule.folder.c_str());
    if (!fileops::clearDirectory(rule.folder)) {
        log::error("lab1: failed to clear folder %s", rule.folder.c_str());
    }
}

}  // namespace lab1
