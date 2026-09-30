#include "DiskMonitor.h"

#include "Config.h"
#include "Logger.h"
#include "PathUtils.h"

#include <cerrno>
#include <chrono>
#include <climits>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <initializer_list>
#include <thread>

#include <fcntl.h>
#include <poll.h>
#include <sys/stat.h>
#include <unistd.h>

namespace diskmon {

namespace {

constexpr const char* kRootPidFile = "/var/run/disk_monitor.pid";
constexpr const char* kUserPidFile = "/tmp/disk_monitor.pid";
constexpr auto kStopTimeout = std::chrono::seconds(5);
constexpr auto kStopPollInterval = std::chrono::milliseconds(100);
constexpr int kFallbackMaxFd = 1024;

volatile std::sig_atomic_t hangupReceived = 0;
volatile std::sig_atomic_t terminateReceived = 0;

extern "C" void onSignal(int signum)
{
    if (signum == SIGHUP) {
        hangupReceived = 1;
    } else if (signum == SIGTERM) {
        terminateReceived = 1;
    }
}

std::string procDir(pid_t pid)
{
    return "/proc/" + std::to_string(pid);
}

std::string readFirstLine(const std::string& path)
{
    std::ifstream file(path);
    std::string line;
    std::getline(file, line);
    return line;
}

bool processExists(pid_t pid)
{
    struct stat info {};
    return stat(procDir(pid).c_str(), &info) == 0;
}

// A zombie ('Z' in /proc/<pid>/stat) has already exited.
bool processRunning(pid_t pid)
{
    const std::string status = readFirstLine(procDir(pid) + "/stat");
    const auto commEnd = status.rfind(')');
    return commEnd != std::string::npos && commEnd + 2 < status.size() && status[commEnd + 2] != 'Z';
}

std::string processName(pid_t pid)
{
    return readFirstLine(procDir(pid) + "/comm");
}

bool waitForExit(pid_t pid)
{
    const auto deadline = std::chrono::steady_clock::now() + kStopTimeout;
    while (processRunning(pid)) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(kStopPollInterval);
    }
    return true;
}

void logErrno(const std::string& what)
{
    const int err = errno;
    logger::error(what + ": " + std::strerror(err));
}

bool forkAndExitParent()
{
    const pid_t pid = fork();
    if (pid < 0) {
        logErrno("fork failed");
        return false;
    }
    if (pid > 0) {
        _exit(EXIT_SUCCESS);
    }
    return true;
}

void closeAllDescriptors()
{
    const long limit = sysconf(_SC_OPEN_MAX);
    const int maxFd = (limit > 0 && limit < INT_MAX) ? static_cast<int>(limit) : kFallbackMaxFd;
    for (int fd = maxFd - 1; fd >= 0; --fd) {
        ::close(fd);
    }
}

bool redirectStandardStreamsToNull()
{
    const int nullFd = ::open("/dev/null", O_RDWR);
    return nullFd == STDIN_FILENO && dup2(nullFd, STDOUT_FILENO) == STDOUT_FILENO
           && dup2(nullFd, STDERR_FILENO) == STDERR_FILENO;
}

}

DiskMonitor& DiskMonitor::instance()
{
    static DiskMonitor monitor;
    return monitor;
}

DiskMonitor::DiskMonitor() : pidFile_(geteuid() == 0 ? kRootPidFile : kUserPidFile) {}

int DiskMonitor::run(const std::string& configFile)
{
    logger::open(logger::Output::SyslogAndStderr);

    if (!loadConfig(configFile)) {
        logger::close();
        return EXIT_FAILURE;
    }

    stopRunningInstance();

    logger::info("Starting daemon: config " + configPath_ + ", pid file " + pidFile_.path());
    if (!daemonize()) {
        logger::close();
        return EXIT_FAILURE;
    }

    installSignalHandlers();
    if (!pidFile_.write(getpid())) {
        logger::error("Disk monitor stopped: cannot write pid file");
        logger::close();
        return EXIT_FAILURE;
    }
    logger::info("Disk monitor started (pid " + std::to_string(getpid()) + ")");

    const bool ok = watcher_.start(directories_) && serve();

    watcher_.stop();
    pidFile_.removeIfOwnedBy(getpid());
    if (ok) {
        logger::info("Disk monitor stopped");
    } else {
        logger::error("Disk monitor stopped due to an error");
    }
    logger::close();
    return ok ? EXIT_SUCCESS : EXIT_FAILURE;
}

// The config path is made absolute before daemonize() changes the working directory to "/"
bool DiskMonitor::loadConfig(const std::string& configFile)
{
    const auto path = paths::canonical(configFile);
    if (!path) {
        logErrno("Config file '" + configFile + "' not found");
        return false;
    }
    configPath_ = *path;

    auto config = Config::load(configPath_);
    if (!config) {
        return false;
    }
    directories_ = std::move(config->directories);
    if (directories_.empty()) {
        logger::warning("No existing directories in " + configPath_ + ": nothing to watch until SIGHUP");
    }
    return true;
}

void DiskMonitor::reloadConfig()
{
    logger::info("SIGHUP received, re-reading config " + configPath_);

    auto config = Config::load(configPath_);
    if (!config) {
        logger::error("Config was not reloaded, keeping previous settings");
        return;
    }
    directories_ = std::move(config->directories);
    if (directories_.empty()) {
        logger::warning("No existing directories in config: nothing to watch");
    }
    if (watcher_.start(directories_)) {
        logger::info("Config reloaded");
    }
}

void DiskMonitor::stopRunningInstance() const
{
    const auto oldPid = pidFile_.read();
    if (!oldPid || *oldPid == getpid()) {
        return;
    }
    const std::string pidText = std::to_string(*oldPid);

    if (!processExists(*oldPid)) {
        logger::info("Stale pid file " + pidFile_.path() + ": process " + pidText + " does not exist");
        return;
    }

    // After a crash the pid may belong to an unrelated process.
    const std::string oldName = processName(*oldPid);
    if (!oldName.empty() && oldName != processName(getpid())) {
        logger::warning("Stale pid file " + pidFile_.path() + ": pid " + pidText + " belongs to '" + oldName
                        + "', not touching it");
        return;
    }

    logger::info("Daemon is already running (pid " + pidText + "), sending SIGTERM");
    if (kill(*oldPid, SIGTERM) != 0) {
        logErrno("Cannot send SIGTERM to " + pidText);
        return;
    }
    if (!waitForExit(*oldPid)) {
        logger::warning("Process " + pidText + " is still running after SIGTERM");
    }
}

bool DiskMonitor::daemonize()
{
    if (!forkAndExitParent()) {
        return false;
    }
    if (setsid() < 0) {
        logErrno("setsid failed");
        return false;
    }
    // Second fork: the daemon is not a session leader and can never reacquire a terminal
    if (!forkAndExitParent()) {
        return false;
    }

    umask(0);
    if (chdir("/") != 0) {
        logErrno("chdir(\"/\") failed");
        return false;
    }

    logger::close();
    closeAllDescriptors();
    const bool redirected = redirectStandardStreamsToNull();
    logger::open(logger::Output::Syslog);

    if (!redirected) {
        logger::error("Cannot redirect standard streams to /dev/null");
        return false;
    }
    return true;
}

void DiskMonitor::installSignalHandlers()
{
    struct sigaction action {};
    action.sa_handler = onSignal;
    sigemptyset(&action.sa_mask);

    sigset_t blocked;
    sigemptyset(&blocked);
    for (const int signum : {SIGHUP, SIGTERM}) {
        if (sigaction(signum, &action, nullptr) != 0) {
            logErrno("sigaction(" + std::to_string(signum) + ") failed");
        }
        sigaddset(&blocked, signum);
    }

    // The signals stay blocked except inside ppoll(), so none is lost between a flag check and the wait
    sigprocmask(SIG_BLOCK, &blocked, &waitMask_);
    sigdelset(&waitMask_, SIGHUP);
    sigdelset(&waitMask_, SIGTERM);
}

bool DiskMonitor::serve()
{
    while (!terminateReceived) {
        if (hangupReceived) {
            hangupReceived = 0;
            reloadConfig();
        }

        pollfd watched{watcher_.fd(), POLLIN, 0};
        const int ready = ppoll(&watched, 1, nullptr, &waitMask_);
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            logErrno("ppoll failed");
            return false;
        }
        if (ready > 0 && (watched.revents & POLLIN)) {
            watcher_.processEvents();
        }
    }

    logger::info("SIGTERM received, exiting");
    return true;
}

}
