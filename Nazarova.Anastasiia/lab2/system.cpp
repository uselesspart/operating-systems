#include "system.hpp"

#include <cerrno>
#include <charconv>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <stdexcept>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <utility>

void systemError(const std::string& operation) {
    throw std::runtime_error(operation + ": " + std::strerror(errno));
}

sigset_t signalSet(int number) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, number);
    return set;
}

void prepareSignals() {
    sigset_t set = signalSet(SIGUSR1);
    sigaddset(&set, SIGUSR2);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    const int error = pthread_sigmask(SIG_BLOCK, &set, nullptr);
    if (error != 0) {
        throw std::runtime_error(std::strerror(error));
    }
    struct sigaction action {};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPIPE, &action, nullptr) < 0) systemError("sigaction");
}

pid_t parsePid(const char* text) {
    pid_t pid = 0;
    const char* end = text + std::strlen(text);
    const auto result = std::from_chars(text, end, pid);
    if (result.ec != std::errc() || result.ptr != end || pid <= 1) {
        throw std::runtime_error("Неверный PID хоста");
    }
    return pid;
}

std::string resourceName(pid_t host, pid_t client, const std::string& suffix) {
    return "/lab2_" + std::to_string(getuid()) + "_" + std::to_string(host)
        + "_" + std::to_string(client) + "_" + suffix;
}

std::filesystem::path hostDirectory(pid_t host) {
    return std::filesystem::temp_directory_path() / resourceName(host, 0, "chat").substr(1);
}

Fd::~Fd() { reset(); }
Fd::Fd(Fd&& other) noexcept : value_(std::exchange(other.value_, -1)) {}
Fd& Fd::operator=(Fd&& other) noexcept {
    if (this != &other) reset(std::exchange(other.value_, -1));
    return *this;
}
void Fd::reset(int value) {
    if (value_ >= 0) close(value_);
    value_ = value;
}

bool ready(int fd, short events, int timeoutMs) {
    pollfd item{fd, events, 0};
    int result;
    do {
        result = poll(&item, 1, timeoutMs);
    } while (result < 0 && errno == EINTR);
    if (result < 0) systemError("poll");
    if (item.revents & POLLNVAL) throw std::runtime_error("Канал закрыт");
    return result > 0;
}

void waitReady(int fd, short events, Clock::time_point deadline) {
    while (Clock::now() < deadline) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
        if (ready(fd, events, static_cast<int>(left.count()) + 1)) return;
    }
    throw std::runtime_error("Время ожидания истекло");
}

Semaphore::Semaphore(std::string name, bool create, unsigned value)
    : name_(std::move(name)), owner_(create),
      value_(sem_open(name_.c_str(), create ? O_CREAT | O_EXCL : 0, 0600, value)) {
    if (value_ == SEM_FAILED) systemError("sem_open");
}
Semaphore::~Semaphore() {
    sem_close(value_);
    if (owner_) sem_unlink(name_.c_str());
}
void Semaphore::wait() {
    timespec deadline{};
    if (clock_gettime(CLOCK_REALTIME, &deadline) < 0) systemError("clock_gettime");
    deadline.tv_sec += connectionTimeout.count();
    while (sem_timedwait(value_, &deadline) < 0) {
        if (errno != EINTR) systemError("sem_timedwait");
    }
}
void Semaphore::post() {
    if (sem_post(value_) < 0) systemError("sem_post");
}

Directory::Directory(std::filesystem::path name, bool create)
    : path(std::move(name)), owner_(create) {
    if (create && mkdir(path.c_str(), 0700) < 0) systemError("mkdir");
}
Directory::~Directory() {
    if (owner_) {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
}

Process::Process(pid_t pid) : fd_(static_cast<int>(syscall(SYS_pidfd_open, pid, 0))) {
    if (fd_.get() < 0) systemError("pidfd_open");
}
bool Process::finished() const { return ready(fd_.get(), POLLIN); }
void Process::send(int signal) const {
    if (syscall(SYS_pidfd_send_signal, fd_.get(), signal, nullptr, 0) < 0) {
        systemError("pidfd_send_signal");
    }
}
