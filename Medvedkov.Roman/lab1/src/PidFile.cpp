#include "PidFile.hpp"

#include <csignal>
#include <filesystem>
#include <sstream>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>

namespace reminder {
namespace {
void failIf(bool condition, const char* operation) {
    if (condition) throw std::runtime_error(std::string(operation) + ": " + std::strerror(errno));
}

int openPrivate(const std::string& path) {
    const int fd = open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) return fd;
    struct stat info{};
    if (fstat(fd, &info) < 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid()) {
        close(fd);
        errno = EACCES;
        return -1;
    }
    return fd;
}

void replacePrevious(int fd) {
    char contents[64]{};
    const auto size = pread(fd, contents, sizeof(contents) - 1, 0);
    failIf(size < 0, "Read pidfile");
    std::string value(contents, static_cast<std::size_t>(size));
    if (value.find_first_not_of("0123456789\n \t") == std::string::npos &&
        value.find_first_of("0123456789") != std::string::npos) {
        long long number = 0;
        std::string extra;
        std::istringstream input(value);
        // Treat overflow and multiple numbers as a corrupt, stale PID file.
        if (!(input >> number) || (input >> extra)) number = 0;
        if (number > 1 && number <= 2147483647 && number != getpid()) {
            const auto proc = "/proc/" + std::to_string(number);
            if (std::filesystem::exists(proc)) {
                // A stale PID must not terminate an unrelated program.
                std::error_code error;
                const auto executable = std::filesystem::read_symlink(proc + "/exe", error);
                const auto self = std::filesystem::read_symlink("/proc/self/exe");
                struct stat info{};
                if (error || executable != self || stat(proc.c_str(), &info) < 0 || info.st_uid != getuid()) {
                    throw std::runtime_error("PID belongs to another process; refusing replacement");
                }
                failIf(kill(static_cast<pid_t>(number), SIGTERM) < 0 && errno != ESRCH, "SIGTERM previous daemon");
            }
        }
    }
    for (unsigned attempt = 0; attempt < 100; ++attempt) {
        if (flock(fd, LOCK_EX | LOCK_NB) == 0) return;
        failIf(errno != EWOULDBLOCK, "Lock pidfile");
        usleep(50000);
    }
    throw std::runtime_error("Previous daemon did not release pidfile within 5 seconds");
}

}

PidFile::PidFile(const std::string& path)
    : launchLock_(openPrivate(path + ".launch")), file_(openPrivate(path)) {
    failIf(flock(launchLock_.get(), LOCK_EX | LOCK_NB) < 0, "Another launcher is running");
    replacePrevious(file_.get());
}

void PidFile::releaseLaunchLock() {
    launchLock_.reset();
}

void PidFile::writeCurrent() {
    const auto number = std::to_string(getpid()) + "\n";
    clear();
    failIf(pwrite(file_.get(), number.data(), number.size(), 0) != static_cast<ssize_t>(number.size()), "Write pidfile");
}

void PidFile::clear() {
    // Keep the inode: every launcher must continue to lock the same file.
    failIf(ftruncate(file_.get(), 0) < 0, "Clear pidfile");
}
}
