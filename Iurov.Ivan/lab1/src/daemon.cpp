#include "treecleaner/daemon.hpp"

#include <cerrno>
#include <algorithm>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;

namespace treecleaner {
namespace {

constexpr auto kTakeoverTimeout = std::chrono::seconds(10);
constexpr auto kRetryDelay = std::chrono::milliseconds(50);

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

pid_t readPidFromFd(int fd) {
    char buffer[64]{};
    ssize_t count;
    do {
        count = ::pread(fd, buffer, sizeof(buffer) - 1, 0);
    } while (count < 0 && errno == EINTR);

    if (count <= 0) {
        return -1;
    }

    try {
        std::size_t consumed = 0;
        const long long value = std::stoll(std::string(buffer, static_cast<std::size_t>(count)), &consumed, 10);
        while (consumed < static_cast<std::size_t>(count) &&
               (buffer[consumed] == ' ' || buffer[consumed] == '\t' || buffer[consumed] == '\r' || buffer[consumed] == '\n')) {
            ++consumed;
        }
        if (consumed != static_cast<std::size_t>(count) || value <= 0 ||
            value > static_cast<long long>(std::numeric_limits<pid_t>::max())) {
            return -1;
        }
        return static_cast<pid_t>(value);
    } catch (const std::exception&) {
        return -1;
    }
}

bool procEntryExists(pid_t pid) {
    if (pid <= 0) {
        return false;
    }
    struct stat info {};
    const std::string procPath = "/proc/" + std::to_string(static_cast<long long>(pid));
    return ::stat(procPath.c_str(), &info) == 0 && S_ISDIR(info.st_mode);
}

std::string readLink(const std::string& path) {
    std::vector<char> buffer(4096);
    const ssize_t length = ::readlink(path.c_str(), buffer.data(), buffer.size() - 1);
    if (length < 0) {
        return {};
    }
    return std::string(buffer.data(), static_cast<std::size_t>(length));
}

std::string executableName(std::string path) {
    constexpr const char deletedSuffix[] = " (deleted)";
    const std::string suffix(deletedSuffix);
    if (path.size() >= suffix.size() && path.compare(path.size() - suffix.size(), suffix.size(), suffix) == 0) {
        path.erase(path.size() - suffix.size());
    }
    return fs::path(path).filename().string();
}

bool processLooksLikeThisDaemon(pid_t pid) {
    const std::string procBase = "/proc/" + std::to_string(static_cast<long long>(pid));
    const std::string targetExe = readLink(procBase + "/exe");
    const std::string thisExe = readLink("/proc/self/exe");
    if (targetExe.empty() || thisExe.empty() || executableName(targetExe) != executableName(thisExe)) {
        return false;
    }

    // A stale PID can refer to an unrelated process after PID reuse. Only
    // signal a process running this executable in daemon mode.
    std::ifstream commandLine(procBase + "/cmdline", std::ios::binary);
    if (!commandLine) {
        return false;
    }
    std::string args((std::istreambuf_iterator<char>(commandLine)), std::istreambuf_iterator<char>());
    return args.find("--daemon") != std::string::npos;
}

bool waitForProcessToStop(pid_t pid, std::chrono::steady_clock::time_point deadline) {
    while (std::chrono::steady_clock::now() < deadline) {
        if (!procEntryExists(pid) || !processLooksLikeThisDaemon(pid)) {
            return true;
        }
        std::this_thread::sleep_for(kRetryDelay);
    }
    return !procEntryExists(pid) || !processLooksLikeThisDaemon(pid);
}

void closeFd(int& fd) noexcept {
    if (fd >= 0) {
        (void)::close(fd);
        fd = -1;
    }
}

} // namespace

PidFile::PidFile(const fs::path& path) : path_(path), fd_(-1) {
    const auto deadline = std::chrono::steady_clock::now() + kTakeoverTimeout;
    std::string lastReason = "истёк срок ожидания освобождения PID-файла";

    // Do not reject a second start immediately on flock failure. The lab
    // requires a new invocation to read the old PID, check /proc, request
    // SIGTERM, wait for shutdown, then take ownership of the PID file.
    while (std::chrono::steady_clock::now() < deadline) {
        fd_ = ::open(path_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
        if (fd_ < 0) {
            throw std::runtime_error("не удалось открыть PID-файл '" + path_.string() + "': " + std::strerror(errno));
        }

        const pid_t previousPid = readPidFromFd(fd_);
        if (previousPid > 0 && previousPid != ::getpid() && procEntryExists(previousPid)) {
            if (!processLooksLikeThisDaemon(previousPid)) {
                // If no process holds the file lock, the PID text is stale;
                // otherwise refuse to kill an unrelated process.
                if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
                    return;
                }
                lastReason = "PID-файл '" + path_.string() + "' указывает на существующий процесс PID " +
                             std::to_string(static_cast<long long>(previousPid)) +
                             ", но это не TreeCleaner; отказ отправлять ему SIGTERM";
                closeFd(fd_);
                throw std::runtime_error(lastReason);
            }

            if (::kill(previousPid, SIGTERM) != 0 && errno != ESRCH) {
                lastReason = "не удалось отправить SIGTERM старому демону PID " +
                             std::to_string(static_cast<long long>(previousPid)) + ": " + std::strerror(errno);
                closeFd(fd_);
                throw std::runtime_error(lastReason);
            }

            const auto processDeadline = std::min(deadline, std::chrono::steady_clock::now() + kTakeoverTimeout);
            closeFd(fd_);
            if (!waitForProcessToStop(previousPid, processDeadline)) {
                throw std::runtime_error("старый демон PID " + std::to_string(static_cast<long long>(previousPid)) +
                                         " не завершился после SIGTERM в течение 10 секунд");
            }
            // The old daemon removes its PID file on shutdown. Re-open by
            // pathname so we never keep a lock on an unlinked old inode.
            std::this_thread::sleep_for(kRetryDelay);
            continue;
        }

        if (::flock(fd_, LOCK_EX | LOCK_NB) == 0) {
            return;
        }

        const int lockError = errno;
        closeFd(fd_);
        if (lockError != EWOULDBLOCK && lockError != EAGAIN && lockError != EINTR) {
            throw std::runtime_error(std::string("не удалось заблокировать PID-файл: ") + std::strerror(lockError));
        }
        lastReason = "PID-файл занят процессом, который ещё не записал PID или завершает запуск";
        std::this_thread::sleep_for(kRetryDelay);
    }

    throw std::runtime_error(lastReason + ": " + path_.string());
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
