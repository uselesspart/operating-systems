#include "Daemon.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <exception>
#include <iostream>
#include <system_error>
#include <utility>

namespace fs = std::filesystem;

namespace {

using Clock = std::chrono::steady_clock;
using Outcome = SafeCleaner::Outcome;

constexpr const char* kIdent = "daemon_lab1";
constexpr int kStartupTimeoutMs = 10000;
constexpr long kMaxDescriptors = 65536;

std::string errnoText(int err) {
    return std::generic_category().message(err);
}

sigset_t signalSet(bool foreground) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGHUP);
    sigaddset(&set, SIGTERM);
    if (foreground) {
        sigaddset(&set, SIGINT);
    }
    return set;
}

void closeDescriptorsExcept(int keep) {
    long limit = ::sysconf(_SC_OPEN_MAX);
    if (limit < 0 || limit > kMaxDescriptors) {
        limit = kMaxDescriptors;
    }
    for (int fd = 0; fd < limit; ++fd) {
        if (fd != keep) {
            ::close(fd);
        }
    }
}

int awaitStartup(int readFd, pid_t child) {
    std::string message;
    const auto deadline = Clock::now() + std::chrono::milliseconds(kStartupTimeoutMs);
    for (;;) {
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now())
                .count();
        if (left <= 0) {
            std::cerr << kIdent << ": daemon (PID " << child
                      << ") did not report readiness within " << kStartupTimeoutMs / 1000
                      << " s\n";
            return 1;
        }
        pollfd pfd{};
        pfd.fd = readFd;
        pfd.events = POLLIN;
        const int ready = ::poll(&pfd, 1, static_cast<int>(left));
        if (ready < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << kIdent << ": poll failed: " << errnoText(errno) << '\n';
            return 1;
        }
        if (ready == 0) {
            continue;
        }
        char buffer[512];
        const ssize_t n = ::read(readFd, buffer, sizeof buffer);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            std::cerr << kIdent << ": read failed: " << errnoText(errno) << '\n';
            return 1;
        }
        if (n == 0) {
            break;
        }
        message.append(buffer, static_cast<std::size_t>(n));
    }

    if (message.compare(0, 2, "OK") == 0) {
        std::cout << kIdent << ": started, PID " << child << std::endl;
        return 0;
    }
    if (message.empty()) {
        std::cerr << kIdent << ": daemon process exited before it became ready\n";
    } else {
        std::cerr << kIdent << ": " << message;
    }
    return 1;
}

}

Daemon& Daemon::instance() {
    static Daemon daemon;
    return daemon;
}

bool Daemon::parseArguments(const std::vector<std::string>& args, Options& options,
                            std::string& error) {
    Options parsed;
    bool configGiven = false;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        if (arg == "--foreground" || arg == "-f") {
            parsed.foreground = true;
        } else if (arg == "--help" || arg == "-h") {
            parsed.help = true;
        } else if (arg == "--pid-file") {
            if (i + 1 >= args.size() || args[i + 1].empty() || args[i + 1][0] == '-') {
                error = "option --pid-file requires a path";
                return false;
            }
            parsed.pidPath = args[++i];
        } else if (arg.compare(0, 11, "--pid-file=") == 0) {
            const std::string value = arg.substr(11);
            if (value.empty()) {
                error = "option --pid-file requires a path";
                return false;
            }
            parsed.pidPath = value;
        } else if (arg.empty()) {
            error = "empty argument";
            return false;
        } else if (arg[0] == '-') {
            error = "unknown option '" + arg + "'";
            return false;
        } else {
            if (configGiven) {
                error = "more than one configuration path given";
                return false;
            }
            parsed.configPath = arg;
            configGiven = true;
        }
    }
    options = std::move(parsed);
    return true;
}

std::string Daemon::usage(const std::string& program) {
    return "Usage: " + program + " [--foreground] [--pid-file PATH] [CONFIG]\n"
           "  CONFIG            configuration file (default: ./config.conf)\n"
           "  --pid-file PATH   PID file (default: /tmp/daemon_lab1.pid)\n"
           "  -f, --foreground  do not daemonize, log to the terminal as well\n"
           "  -h, --help        show this help\n";
}

fs::path Daemon::resolvePath(const fs::path& path, std::string& error) {
    if (path.empty()) {
        error = "empty path";
        return fs::path();
    }
    std::error_code ec;
    const fs::path absolute = fs::absolute(path, ec);
    if (ec) {
        error = "cannot make '" + path.string() + "' absolute: " + ec.message();
        return fs::path();
    }
    const fs::path name = absolute.filename();
    if (name.empty() || name == "." || name == "..") {
        error = "'" + path.string() + "' does not name a file";
        return fs::path();
    }
    const fs::path parent = fs::weakly_canonical(absolute.parent_path(), ec);
    if (ec) {
        error = "cannot resolve directory of '" + path.string() + "': " + ec.message();
        return fs::path();
    }
    return parent / name;
}

std::vector<Daemon::LogLine> Daemon::describe(const SafeCleaner::Report& report) {
    const std::string folder = "folder '" + report.folder.string() + "': ";
    const std::string counts = std::to_string(report.removedFiles) + " files, " +
                               std::to_string(report.removedDirs) + " directories";
    const std::size_t removed = report.removedFiles + report.removedDirs;

    std::vector<LogLine> lines;
    Logger::Level level = Logger::Level::Info;
    switch (report.outcome) {
        case Outcome::Cleaned:
            if (removed > 0) {
                lines.push_back({Logger::Level::Info, folder + "cleaned, removed " + counts});
            }
            return lines;
        case Outcome::Protected:
            lines.push_back(
                {Logger::Level::Info, folder + "protective file present, nothing removed"});
            return lines;
        case Outcome::Interrupted:
            level = Logger::Level::Warning;
            lines.push_back({level, folder + "cleanup interrupted after removing " + counts});
            break;
        case Outcome::Rejected:
            level = Logger::Level::Error;
            lines.push_back({level, folder + "rejected, nothing removed"});
            break;
        case Outcome::Failed:
            level = Logger::Level::Error;
            lines.push_back({level, folder + "cleanup failed, removed " + counts});
            break;
    }
    for (const std::string& message : report.messages) {
        lines.push_back({level, folder + message});
    }
    return lines;
}

int Daemon::run(const Options& options) {
    const int code = runImpl(options);
    Logger::close();
    return code;
}

int Daemon::runImpl(const Options& options) {
    foreground_ = options.foreground;
    Logger::open(kIdent, true);

    if (::geteuid() == 0) {
        Logger::error("refusing to run as root: start the daemon as an unprivileged user");
        return 1;
    }

    std::string error;
    configPath_ = resolvePath(options.configPath, error);
    if (configPath_.empty()) {
        Logger::error("invalid configuration path: " + error);
        return 1;
    }
    pidPath_ = resolvePath(options.pidPath, error);
    if (pidPath_.empty()) {
        Logger::error("invalid PID file path: " + error);
        return 1;
    }

    Config initial;
    std::unique_ptr<SafeCleaner> cleaner;
    if (!prepareConfiguration(initial, cleaner)) {
        Logger::error("startup aborted: invalid configuration");
        return 1;
    }
    config_ = std::move(initial);
    cleaner_ = std::move(cleaner);

    pidFile_ = std::make_unique<PidFile>(pidPath_);
    const PidFile::Previous previous = pidFile_->stopPrevious();
    switch (previous.kind) {
        case PidFile::Previous::Kind::None:
            break;
        case PidFile::Previous::Kind::Stale:
        case PidFile::Previous::Kind::Stopped:
            Logger::info(previous.message);
            break;
        case PidFile::Previous::Kind::Failed:
            Logger::error("cannot start: " + previous.message);
            return 1;
    }

    const sigset_t set = signalSet(foreground_);
    if (::sigprocmask(SIG_BLOCK, &set, nullptr) != 0) {
        Logger::error("cannot block signals: " + errnoText(errno));
        return 1;
    }
    ::signal(SIGPIPE, SIG_IGN);

    if (!foreground_) {
        int exitCode = 0;
        if (!daemonize(exitCode)) {
            return exitCode;
        }
    }

    std::string pidError;
    if (!pidFile_->acquire(pidError)) {
        Logger::error("cannot start: " + pidError);
        reportStartup(false, pidError);
        return 1;
    }

    Logger::info("daemon started (PID " + std::to_string(::getpid()) + ", config '" +
                 configPath_.string() + "', " + summary() + ")");
    reportStartup(true, "");

    const int code = loop();
    pidFile_->release();
    Logger::info("daemon stopped");
    return code;
}

bool Daemon::prepareConfiguration(Config& config, std::unique_ptr<SafeCleaner>& cleaner) {
    Config fresh;
    std::vector<std::string> errors;
    if (!Config::load(configPath_, fresh, errors)) {
        for (const std::string& message : errors) {
            Logger::error("configuration error: " + message);
        }
        return false;
    }

    auto candidate =
        std::make_unique<SafeCleaner>(SafeCleaner::Policy::standard(configPath_, pidPath_));
    errors = candidate->validate(fresh.rules());
    if (!errors.empty()) {
        for (const std::string& message : errors) {
            Logger::error("configuration error: " + message);
        }
        return false;
    }

    config = std::move(fresh);
    cleaner = std::move(candidate);
    return true;
}

bool Daemon::daemonize(int& exitCode) {
    int fds[2];
    if (::pipe(fds) != 0) {
        Logger::error("cannot create startup pipe: " + errnoText(errno));
        exitCode = 1;
        return false;
    }
    if (fds[1] <= 2) {
        const int moved = ::fcntl(fds[1], F_DUPFD, 3);
        if (moved < 0) {
            Logger::error("cannot move startup pipe: " + errnoText(errno));
            ::close(fds[0]);
            ::close(fds[1]);
            exitCode = 1;
            return false;
        }
        ::close(fds[1]);
        fds[1] = moved;
    }

    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);

    const pid_t pid = ::fork();
    if (pid < 0) {
        Logger::error("fork failed: " + errnoText(errno));
        ::close(fds[0]);
        ::close(fds[1]);
        exitCode = 1;
        return false;
    }
    if (pid > 0) {
        ::close(fds[1]);
        exitCode = awaitStartup(fds[0], pid);
        ::close(fds[0]);
        return false;
    }

    ::close(fds[0]);
    startupFd_ = fds[1];
    return becomeDaemon(exitCode);
}

bool Daemon::becomeDaemon(int& exitCode) {
    const auto failStart = [&](const std::string& what, int err) {
        const std::string message = what + ": " + errnoText(err);
        Logger::error(message);
        reportStartup(false, message);
        exitCode = 1;
        return false;
    };

    if (::setsid() < 0) {
        return failStart("setsid failed", errno);
    }
    ::umask(022);
    if (::chdir("/") != 0) {
        return failStart("chdir(\"/\") failed", errno);
    }

    Logger::close();
    closeDescriptorsExcept(startupFd_);
    const int nullFd = ::open("/dev/null", O_RDWR);
    const int nullErr = errno;
    Logger::open(kIdent, false);
    if (nullFd < 0) {
        return failStart("cannot open /dev/null", nullErr);
    }
    if (::dup2(nullFd, 0) < 0 || ::dup2(nullFd, 1) < 0 || ::dup2(nullFd, 2) < 0) {
        return failStart("dup2 failed", errno);
    }
    if (nullFd > 2) {
        ::close(nullFd);
    }
    return true;
}

void Daemon::reportStartup(bool ok, const std::string& message) {
    if (startupFd_ < 0) {
        return;
    }
    const std::string text = ok ? std::string("OK\n") : "ERROR: " + Logger::sanitize(message) + "\n";
    const ssize_t n = ::write(startupFd_, text.data(), text.size());
    static_cast<void>(n);
    ::close(startupFd_);
    startupFd_ = -1;
}

int Daemon::loop() {
    const sigset_t set = signalSet(foreground_);
    auto deadline = Clock::now();
    for (;;) {
        if (Clock::now() >= deadline) {
            try {
                runCycle();
            } catch (const std::exception& e) {
                Logger::error(std::string("cleanup cycle failed: ") + e.what());
            }
            deadline = Clock::now() + std::chrono::seconds(config_.interval());
        }

        const Clock::duration remaining =
            std::max(deadline - Clock::now(), Clock::duration::zero());
        const long long ns =
            std::chrono::duration_cast<std::chrono::nanoseconds>(remaining).count();
        timespec timeout{};
        timeout.tv_sec = static_cast<time_t>(ns / 1000000000LL);
        timeout.tv_nsec = static_cast<long>(ns % 1000000000LL);

        const int sig = ::sigtimedwait(&set, nullptr, &timeout);
        if (sig < 0) {
            if (errno == EAGAIN || errno == EINTR) {
                continue;
            }
            Logger::error("sigtimedwait failed: " + errnoText(errno));
            return 1;
        }
        if (sig == SIGTERM) {
            Logger::info("SIGTERM received, daemon is exiting");
            return 0;
        }
        if (sig == SIGINT) {
            Logger::info("SIGINT received, daemon is exiting");
            return 0;
        }
        if (sig == SIGHUP && reload()) {
            deadline = std::min(deadline, Clock::now() + std::chrono::seconds(config_.interval()));
        }
    }
}

void Daemon::runCycle() {
    const std::vector<SafeCleaner::Report> reports = cleaner_->cleanAll(config_.rules());
    for (const SafeCleaner::Report& report : reports) {
        const std::string key = report.folder.string();
        const auto known = lastOutcome_.find(key);
        const bool repeated = known != lastOutcome_.end() && known->second == report.outcome;
        lastOutcome_[key] = report.outcome;

        const bool quietWhenRepeated = report.outcome == Outcome::Protected ||
                                       report.outcome == Outcome::Rejected ||
                                       report.outcome == Outcome::Failed;
        if (repeated && quietWhenRepeated) {
            continue;
        }
        for (const LogLine& line : describe(report)) {
            Logger::log(line.level, line.text);
        }
    }
}

bool Daemon::reload() {
    Logger::info("SIGHUP received, reloading configuration '" + configPath_.string() + "'");
    Config fresh;
    std::unique_ptr<SafeCleaner> cleaner;
    if (!prepareConfiguration(fresh, cleaner)) {
        Logger::warning("configuration reload failed, keeping previous settings");
        return false;
    }
    config_ = std::move(fresh);
    cleaner_ = std::move(cleaner);
    lastOutcome_.clear();
    Logger::info("configuration reloaded (" + summary() + ")");
    return true;
}

std::string Daemon::summary() const {
    return "interval " + std::to_string(config_.interval()) + " s, " +
           std::to_string(config_.rules().size()) + " folder rule(s)";
}
