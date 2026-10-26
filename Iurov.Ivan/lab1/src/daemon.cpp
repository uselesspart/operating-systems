#include "treecleaner/daemon.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace fs = std::filesystem;

namespace treecleaner {
namespace {

void writeAll(int fd, const std::string& text) {
    const char* data = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, data, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            throw std::runtime_error(std::string("не удалось записать PID-файл: ") + std::strerror(errno));
        }
        if (written == 0) {
            throw std::runtime_error("не удалось записать PID-файл: запись вернула 0 байт");
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
}

} // namespace

PidFile::PidFile(const fs::path& path) : path_(path), fd_(-1) {
    fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (fd_ < 0) {
        throw std::runtime_error("не удалось открыть PID-файл '" + path_.string() + "': " + std::strerror(errno));
    }
    if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
        const std::string reason = (errno == EWOULDBLOCK || errno == EAGAIN)
            ? "демон уже запущен (PID-файл заблокирован)"
            : std::string("не удалось заблокировать PID-файл: ") + std::strerror(errno);
        ::close(fd_);
        fd_ = -1;
        throw std::runtime_error(reason + ": " + path_.string());
    }
}

PidFile::~PidFile() {
    if (fd_ >= 0) {
        ::close(fd_);
    }
}

void PidFile::writeCurrentPid() {
    if (::ftruncate(fd_, 0) != 0 || ::lseek(fd_, 0, SEEK_SET) < 0) {
        throw std::runtime_error(std::string("не удалось очистить PID-файл: ") + std::strerror(errno));
    }
    writeAll(fd_, std::to_string(static_cast<long long>(::getpid())) + "\n");
    if (::fsync(fd_) != 0) {
        throw std::runtime_error(std::string("не удалось синхронизировать PID-файл: ") + std::strerror(errno));
    }
}

void PidFile::removeIfOwned() noexcept {
    if (cleaned_ || fd_ < 0) {
        return;
    }
    cleaned_ = true;
    struct stat descriptorStat {};
    struct stat pathStat {};
    if (::fstat(fd_, &descriptorStat) == 0 && ::lstat(path_.c_str(), &pathStat) == 0 &&
        descriptorStat.st_dev == pathStat.st_dev && descriptorStat.st_ino == pathStat.st_ino) {
        (void)::unlink(path_.c_str());
    }
}

DaemonizeResult daemonize(std::string& errorMessage) {
    const pid_t pid = ::fork();
    if (pid < 0) {
        errorMessage = std::string("fork завершился ошибкой: ") + std::strerror(errno);
        return DaemonizeResult::Error;
    }
    if (pid > 0) {
        std::cout << "Демон запущен, PID: " << static_cast<long long>(pid) << std::endl;
        return DaemonizeResult::Parent;
    }

    if (::setsid() < 0) {
        errorMessage = std::string("setsid завершился ошибкой: ") + std::strerror(errno);
        return DaemonizeResult::Error;
    }
    (void)::umask(027);
    if (::chdir("/") != 0) {
        errorMessage = std::string("chdir('/') завершился ошибкой: ") + std::strerror(errno);
        return DaemonizeResult::Error;
    }

    const int nullFd = ::open("/dev/null", O_RDWR);
    if (nullFd < 0) {
        errorMessage = std::string("не удалось открыть /dev/null: ") + std::strerror(errno);
        return DaemonizeResult::Error;
    }
    for (int targetFd = STDIN_FILENO; targetFd <= STDERR_FILENO; ++targetFd) {
        if (::dup2(nullFd, targetFd) < 0) {
            errorMessage = std::string("dup2('/dev/null') завершился ошибкой: ") + std::strerror(errno);
            if (nullFd > STDERR_FILENO) {
                ::close(nullFd);
            }
            return DaemonizeResult::Error;
        }
    }
    if (nullFd > STDERR_FILENO) {
        ::close(nullFd);
    }
    return DaemonizeResult::Child;
}

} // namespace treecleaner
