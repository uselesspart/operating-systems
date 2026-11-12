#include "Daemonize.hpp"

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <vector>

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace diskmond {

namespace {

constexpr const char* kReadyPrefix = "ready ";
constexpr const char* kErrorPrefix = "error ";
constexpr std::size_t kMaxReportSize = 4096;

constexpr long kMaxDescriptorsToClose = 65536;

std::string describeErrno(const std::string& what, int code) {
    return what + ": " + std::strerror(code);
}

bool startsWith(const std::string& text, const char* prefix) {
    return text.compare(0, std::strlen(prefix), prefix) == 0;
}


int keepAboveStandardStreams(int fd) {
    if (fd > STDERR_FILENO) {
        return fd;
    }
    const int moved = ::fcntl(fd, F_DUPFD_CLOEXEC, STDERR_FILENO + 1);
    ::close(fd);
    return moved;
}


std::vector<int> openDescriptors() {
    std::vector<int> result;
    DIR* directory = ::opendir("/proc/self/fd");
    if (directory == nullptr) {
        const long limit = ::sysconf(_SC_OPEN_MAX);
        const long count = (limit > 0 && limit < kMaxDescriptorsToClose) ? limit : kMaxDescriptorsToClose;
        for (long fd = 0; fd < count; ++fd) {
            result.push_back(static_cast<int>(fd));
        }
        return result;
    }
    const int own = ::dirfd(directory);
    while (const dirent* entry = ::readdir(directory)) {
        char* end = nullptr;
        const long fd = std::strtol(entry->d_name, &end, 10);
        if (end != entry->d_name && *end == '\0' && fd != own && fd >= 0 && fd <= INT_MAX) {
            result.push_back(static_cast<int>(fd));
        }
    }
    ::closedir(directory);
    return result;
}

}  

bool StartupChannel::open(std::string& error) {
    int ends[2];
    if (::pipe2(ends, O_CLOEXEC) != 0) {
        const int code = errno;
        error = describeErrno("pipe2 failed", code);
        return false;
    }
    read_.reset(keepAboveStandardStreams(ends[0]));
    write_.reset(keepAboveStandardStreams(ends[1]));
    if (!read_.isOpen() || !write_.isOpen()) {
        error = "cannot set up the startup channel";
        return false;
    }
    return true;
}

void StartupChannel::useAsLauncher() {
    write_.reset();
}

void StartupChannel::useAsDaemon() {
    read_.reset();
}

void StartupChannel::reportReady(pid_t pid) {
    send(kReadyPrefix + std::to_string(pid) + "\n");
}

void StartupChannel::reportFailure(const std::string& reason) {
    send(kErrorPrefix + reason + "\n");
}

void StartupChannel::send(const std::string& text) {
    
    std::size_t sent = 0;
    while (write_.isOpen() && sent < text.size()) {
        const ssize_t written = ::write(write_.get(), text.data() + sent, text.size() - sent);
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written <= 0) {
            break;
        }
        sent += static_cast<std::size_t>(written);
    }
    write_.reset();  
}

bool StartupChannel::awaitReport(std::string& message) {
    std::string report;
    char buffer[512];
    while (read_.isOpen() && report.size() < kMaxReportSize) {
        const ssize_t length = ::read(read_.get(), buffer, sizeof buffer);
        if (length < 0 && errno == EINTR) {
            continue;
        }
        if (length <= 0) {
            break;  
        }
        report.append(buffer, static_cast<std::size_t>(length));
    }
    read_.reset();

    while (!report.empty() && report.back() == '\n') {
        report.pop_back();
    }
    if (startsWith(report, kReadyPrefix)) {
        message = "running in background, pid " + report.substr(std::strlen(kReadyPrefix));
        return true;
    }
    if (startsWith(report, kErrorPrefix)) {
        message = "startup failed: " + report.substr(std::strlen(kErrorPrefix));
    } else {
        message = "the daemon exited during startup, see the system log";
    }
    return false;
}

std::optional<ProcessRole> detachFromTerminal(StartupChannel& channel, std::string& error) {
    
    std::cout.flush();
    std::cerr.flush();
    std::fflush(nullptr);

    const pid_t child = ::fork();
    if (child < 0) {
        const int code = errno;
        error = describeErrno("fork failed", code);
        return std::nullopt;
    }
    if (child > 0) {
        channel.useAsLauncher();
        
        int status = 0;
        while (::waitpid(child, &status, 0) < 0 && errno == EINTR) {
        }
        return ProcessRole::Launcher;
    }

    
    channel.useAsDaemon();
    if (::setsid() < 0) {
        const int code = errno;
        channel.reportFailure(describeErrno("setsid failed", code));
        ::_exit(EXIT_FAILURE);
    }

    
    const pid_t grandchild = ::fork();
    if (grandchild < 0) {
        const int code = errno;
        channel.reportFailure(describeErrno("second fork failed", code));
        ::_exit(EXIT_FAILURE);
    }
    if (grandchild > 0) {
        ::_exit(EXIT_SUCCESS);  
    }
    return ProcessRole::Daemon;
}

bool settleAsDaemon(int keepFd, std::string& error) {
    ::umask(0);  

    
    if (::chdir("/") != 0) {
        const int code = errno;
        error = describeErrno("chdir(\"/\") failed", code);
        return false;
    }

    for (const int fd : openDescriptors()) {
        if (fd != keepFd) {
            ::close(fd);
        }
    }

    const int devNull = ::open("/dev/null", O_RDWR);
    if (devNull < 0) {
        const int code = errno;
        error = describeErrno("cannot open /dev/null", code);
        return false;
    }
    for (const int stream : {STDIN_FILENO, STDOUT_FILENO, STDERR_FILENO}) {
        if (devNull != stream && ::dup2(devNull, stream) < 0) {
            const int code = errno;
            error = describeErrno("cannot redirect standard streams to /dev/null", code);
            return false;
        }
    }
    if (devNull > STDERR_FILENO) {
        ::close(devNull);
    }
    return true;
}

}  
