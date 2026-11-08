#include "Daemon.hpp"

#include <fcntl.h>
#include <poll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <syslog.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iostream>

namespace fs = std::filesystem;

namespace {

const char* const kIdent = "lab1_daemon";
const char* const kPidFile = "/tmp/lab1_daemon.pid";
const char* const kSystemLogDir = "/var/log";

void logInfo(const std::string& msg) { syslog(LOG_INFO, "%s", msg.c_str()); }
void logError(const std::string& msg) { syslog(LOG_ERR, "%s", msg.c_str()); }
void logDiskEvent(const std::string& msg) { syslog(LOG_LOCAL0 | LOG_INFO, "%s", msg.c_str()); }

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

std::string readFirstLine(const std::string& path) {
    std::ifstream f(path);
    std::string line;
    if (f) std::getline(f, line);
    return trim(line);
}

bool processAlive(pid_t pid) {
    std::ifstream f("/proc/" + std::to_string(pid) + "/stat");
    if (!f) return false;
    std::string line;
    std::getline(f, line);
    const auto pos = line.rfind(')');
    if (pos == std::string::npos || pos + 2 >= line.size()) return false;
    return line[pos + 2] != 'Z';
}

void sleepMs(long ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&ts, nullptr);
}

bool isInside(const fs::path& child, const fs::path& parent) {
    auto c = child.begin();
    for (auto p = parent.begin(); p != parent.end(); ++p, ++c) {
        if (c == child.end() || *c != *p) return false;
    }
    return true;
}

const uint32_t kWatchMask = IN_ACCESS | IN_MODIFY | IN_ATTRIB | IN_OPEN | IN_CLOSE_WRITE |
                            IN_CLOSE_NOWRITE | IN_CREATE | IN_DELETE | IN_MOVED_FROM |
                            IN_MOVED_TO | IN_DELETE_SELF | IN_MOVE_SELF;

const char* eventName(uint32_t m) {
    if (m & IN_ACCESS) return "ACCESS";
    if (m & IN_MODIFY) return "MODIFY";
    if (m & IN_ATTRIB) return "ATTRIB";
    if (m & IN_OPEN) return "OPEN";
    if (m & IN_CLOSE_WRITE) return "CLOSE_WRITE";
    if (m & IN_CLOSE_NOWRITE) return "CLOSE_NOWRITE";
    if (m & IN_CREATE) return "CREATE";
    if (m & IN_DELETE) return "DELETE";
    if (m & IN_MOVED_FROM) return "MOVED_FROM";
    if (m & IN_MOVED_TO) return "MOVED_TO";
    if (m & IN_DELETE_SELF) return "DELETE_SELF";
    if (m & IN_MOVE_SELF) return "MOVE_SELF";
    return "UNKNOWN";
}

}

volatile std::sig_atomic_t Daemon::gotTerm_ = 0;
volatile std::sig_atomic_t Daemon::gotHup_ = 0;

Daemon& Daemon::instance() {
    static Daemon d;
    return d;
}

void Daemon::onSignal(int sig) {
    if (sig == SIGTERM) gotTerm_ = 1;
    else if (sig == SIGHUP) gotHup_ = 1;
}

bool Daemon::loadConfig(std::string& error) {
    std::ifstream in(configPath_);
    if (!in) {
        error = "cannot open config " + configPath_ + ": " + std::strerror(errno);
        return false;
    }

    std::vector<std::string> rawFolders;
    bool recursive = true;

    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) {
            error = "config line " + std::to_string(lineNo) + ": expected key = value";
            return false;
        }
        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));

        if (key == "folder") {
            if (val.empty()) {
                error = "config line " + std::to_string(lineNo) + ": empty folder";
                return false;
            }
            rawFolders.push_back(val);
        } else if (key == "recursive") {
            if (val == "yes" || val == "true" || val == "1") recursive = true;
            else if (val == "no" || val == "false" || val == "0") recursive = false;
            else {
                error = "config line " + std::to_string(lineNo) + ": recursive must be yes or no";
                return false;
            }
        } else {
            error = "config line " + std::to_string(lineNo) + ": unknown key '" + key + "'";
            return false;
        }
    }

    if (rawFolders.empty()) {
        error = "config must contain at least one 'folder'";
        return false;
    }

    std::vector<fs::path> folders;
    for (const auto& raw : rawFolders) {
        fs::path p(raw);
        if (p.is_relative()) p = fs::path(configPath_).parent_path() / p;

        std::error_code ec;
        const fs::path canon = fs::canonical(p, ec);
        if (ec) {
            error = "folder '" + p.string() + "': " + ec.message();
            return false;
        }
        if (!fs::is_directory(canon, ec)) {
            error = "'" + canon.string() + "' is not a directory";
            return false;
        }
        // Журнал пишется в /var/log: слежение за ним породило бы бесконечный цикл.
        if (isInside(fs::path(kSystemLogDir), canon) && (recursive || canon == kSystemLogDir)) {
            error = "'" + canon.string() + "' contains the system log directory, refusing";
            return false;
        }
        if (isInside(canon, fs::path(kSystemLogDir))) {
            error = "'" + canon.string() + "' is inside the system log directory, refusing";
            return false;
        }
        if (std::find(folders.begin(), folders.end(), canon) == folders.end()) folders.push_back(canon);
    }

    folders_ = folders;
    recursive_ = recursive;
    return true;
}

bool Daemon::stopPreviousInstance() {
    const std::string content = readFirstLine(kPidFile);
    if (content.empty() || content.find_first_not_of("0123456789") != std::string::npos) return true;

    const pid_t old = static_cast<pid_t>(std::stol(content));
    if (old <= 0 || old == getpid()) return true;

    if (!processAlive(old)) {
        logInfo("stale pid file (process " + content + " is not running)");
        return true;
    }

    const std::string oldName = readFirstLine("/proc/" + content + "/comm");
    const std::string myName = readFirstLine("/proc/self/comm");
    if (oldName != myName) {
        logInfo("pid " + content + " belongs to another program ('" + oldName + "'), not touching it");
        return true;
    }

    logInfo("another instance is running (pid " + content + "), sending SIGTERM");
    if (kill(old, SIGTERM) != 0 && errno != ESRCH) {
        logError(std::string("kill failed: ") + std::strerror(errno));
        return false;
    }
    for (int i = 0; i < 50; ++i) {  // до 5 секунд
        if (!processAlive(old)) return true;
        sleepMs(100);
    }
    logError("previous instance did not exit in time");
    return false;
}

bool Daemon::daemonize() {
    const pid_t pid = fork();
    if (pid < 0) {
        logError(std::string("fork failed: ") + std::strerror(errno));
        return false;
    }
    if (pid > 0) _exit(EXIT_SUCCESS);

    if (setsid() < 0) {
        logError(std::string("setsid failed: ") + std::strerror(errno));
        return false;
    }
    umask(022);
    if (chdir("/") != 0) {
        logError(std::string("chdir failed: ") + std::strerror(errno));
        return false;
    }

    long maxfd = sysconf(_SC_OPEN_MAX);
    if (maxfd < 0 || maxfd > 4096) maxfd = 4096;
    for (int fd = 0; fd < maxfd; ++fd) close(fd);

    const int fd0 = open("/dev/null", O_RDWR);
    if (fd0 != 0) return false;
    if (dup2(fd0, 1) < 0 || dup2(fd0, 2) < 0) return false;
    return true;
}

bool Daemon::writePidFile() {
    const int fd = open(kPidFile, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return false;
    const std::string s = std::to_string(getpid()) + "\n";
    const ssize_t n = write(fd, s.data(), s.size());
    close(fd);
    return n == static_cast<ssize_t>(s.size());
}

void Daemon::removePidFile() {
    if (readFirstLine(kPidFile) == std::to_string(getpid())) unlink(kPidFile);
}

void Daemon::setupSignals() {
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = &Daemon::onSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGHUP, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
}

void Daemon::closeWatches() {
    if (inotifyFd_ >= 0) close(inotifyFd_);
    inotifyFd_ = -1;
    watches_.clear();
}

void Daemon::addWatch(const fs::path& dir) {
    const int wd = inotify_add_watch(inotifyFd_, dir.c_str(), kWatchMask);
    if (wd < 0) {
        logError("cannot watch " + dir.string() + ": " + std::strerror(errno));
        return;
    }
    watches_[wd] = dir;
}

void Daemon::addWatchTree(const fs::path& root) {
    std::vector<fs::path> dirs{root};
    if (recursive_) {
        std::error_code ec;
        fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
        const fs::recursive_directory_iterator end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code ec2;
            if (it->symlink_status(ec2).type() == fs::file_type::directory && !ec2)
                dirs.push_back(it->path());
        }
    }
    for (const auto& d : dirs) addWatch(d);
}

bool Daemon::setupWatches() {
    closeWatches();
    inotifyFd_ = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotifyFd_ < 0) {
        logError(std::string("inotify_init failed: ") + std::strerror(errno));
        return false;
    }
    for (const auto& f : folders_) addWatchTree(f);
    if (watches_.empty()) {
        logError("no directories could be watched");
        return false;
    }
    logInfo("watching " + std::to_string(watches_.size()) + " director" +
            (watches_.size() == 1 ? "y" : "ies") + " (" + std::to_string(folders_.size()) +
            " configured folder(s), recursive=" + (recursive_ ? "yes" : "no") + ")");
    return true;
}

void Daemon::handleEvent(const struct inotify_event& ev) {
    if (ev.mask & IN_Q_OVERFLOW) {
        logError("inotify event queue overflow, some events were lost");
        return;
    }
    const auto it = watches_.find(ev.wd);
    if (ev.mask & IN_IGNORED) {
        if (it != watches_.end()) watches_.erase(it);
        return;
    }
    if (it == watches_.end()) return;

    fs::path path = it->second;
    if (ev.len > 0 && ev.name[0] != '\0') path /= ev.name;
    const bool isDir = (ev.mask & IN_ISDIR) != 0;

    logDiskEvent(std::string(eventName(ev.mask)) + " " + (isDir ? "dir " : "file ") + path.string());

    if (recursive_ && isDir && (ev.mask & (IN_CREATE | IN_MOVED_TO))) addWatchTree(path);
}

void Daemon::readEvents() {
    alignas(struct inotify_event) char buf[16384];
    while (true) {
        const ssize_t n = read(inotifyFd_, buf, sizeof(buf));
        if (n < 0) {
            if (errno != EAGAIN && errno != EINTR) logError(std::string("read failed: ") + std::strerror(errno));
            break;
        }
        if (n == 0) break;
        for (char* p = buf; p < buf + n;) {
            const auto* ev = reinterpret_cast<const struct inotify_event*>(p);
            handleEvent(*ev);
            p += sizeof(struct inotify_event) + ev->len;
        }
    }
}

void Daemon::mainLoop() {
    sigset_t block, orig;
    sigemptyset(&block);
    sigaddset(&block, SIGHUP);
    sigaddset(&block, SIGTERM);
    sigprocmask(SIG_BLOCK, &block, &orig);

    while (true) {
        if (gotTerm_) break;
        if (gotHup_) {
            gotHup_ = 0;
            logInfo("SIGHUP received, reloading config " + configPath_);
            std::string err;
            if (!loadConfig(err)) {
                logError("config reload failed, keeping old settings: " + err);
            } else if (!setupWatches()) {
                logError("cannot apply new config");
            } else {
                logInfo("config reloaded");
            }
        }

        struct pollfd pfd;
        pfd.fd = inotifyFd_;
        pfd.events = POLLIN;
        pfd.revents = 0;
        const int r = ppoll(&pfd, 1, nullptr, &orig);
        if (r > 0 && (pfd.revents & POLLIN)) readEvents();
    }
}

int Daemon::run(const std::string& configPath) {
    std::error_code ec;
    const fs::path abs = fs::absolute(configPath, ec);
    if (ec) {
        std::cerr << "bad config path: " << ec.message() << "\n";
        return EXIT_FAILURE;
    }
    configPath_ = abs.lexically_normal().string();

    openlog(kIdent, LOG_PID, LOG_DAEMON);

    std::string err;
    if (!loadConfig(err)) {
        std::cerr << "error: " << err << "\n";
        logError("startup failed: " + err);
        closelog();
        return EXIT_FAILURE;
    }
    if (!stopPreviousInstance()) {
        std::cerr << "error: cannot stop previous instance\n";
        closelog();
        return EXIT_FAILURE;
    }
    if (!daemonize()) {
        logError("daemonization failed");
        closelog();
        return EXIT_FAILURE;
    }

    closelog();
    openlog(kIdent, LOG_PID, LOG_DAEMON);

    if (!writePidFile()) {
        logError(std::string("cannot write pid file: ") + std::strerror(errno));
        closelog();
        return EXIT_FAILURE;
    }
    if (!setupWatches()) {
        removePidFile();
        closelog();
        return EXIT_FAILURE;
    }

    setupSignals();
    logInfo("daemon started, config=" + configPath_);

    mainLoop();

    logInfo("SIGTERM received, daemon is exiting");
    closeWatches();
    removePidFile();
    closelog();
    return EXIT_SUCCESS;
}
