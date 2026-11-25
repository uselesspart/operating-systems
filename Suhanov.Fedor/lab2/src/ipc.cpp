#include "ipc.hpp"

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstring>
#include <filesystem>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <sys/stat.h>
#include <unistd.h>

namespace application {
namespace {
std::string semaphore_prefix(const std::string& root, int pid) {
    return "/pn" + std::string(TYPE_CODE) + "_" +
           root.substr(root.find_last_of('_') + 1) + "_" + std::to_string(pid);
}
sigset_t signal_set(bool introductions, bool ack = false) {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    if (introductions) { sigaddset(&set, SIGUSR1); }
    if (ack) { sigaddset(&set, SIGUSR2); }
    return set;
}
std::pair<int, int> timed_signal(const sigset_t& set, std::chrono::milliseconds duration) {
    const auto end = Clock::now() + duration;
    do {
        const auto remaining = std::max(std::chrono::nanoseconds::zero(),
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - Clock::now()));
        timespec timeout{static_cast<time_t>(remaining.count() / 1000000000),
                         static_cast<long>(remaining.count() % 1000000000)};
        siginfo_t info{};
        const int result = sigtimedwait(&set, &info, &timeout);
        if (result >= 0) { return {result, info.si_pid}; }
        if (errno == EAGAIN) { return {0, 0}; }
        if (errno != EINTR) { system_error("sigtimedwait"); }
    } while (Clock::now() < end);
    return {0, 0};
}
} // namespace

[[noreturn]] void system_error(const std::string& operation) {
    const int error = errno;
    throw Error(operation + ": " + std::strerror(error));
}
bool process_alive(int pid) { return pid > 1 && (kill(pid, 0) == 0 || errno == EPERM); }
std::string root_path(int host_pid) {
    return "/tmp/polynet_" + std::to_string(getuid()) + "_" + TYPE_CODE + "_" +
           std::to_string(host_pid);
}
void prepare_signals() {
    auto set = signal_set(true, true);
    const int error = pthread_sigmask(SIG_BLOCK, &set, nullptr);
    if (error) { throw Error("pthread_sigmask: " + std::string(std::strerror(error))); }
    struct sigaction action{};
    action.sa_handler = SIG_IGN;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGPIPE, &action, nullptr) < 0) { system_error("sigaction(SIGPIPE)"); }
}
std::pair<int, int> wait_signal(bool introductions, std::chrono::milliseconds timeout) {
    return timed_signal(signal_set(introductions), timeout);
}
int wait_for_ack(int host_pid) {
    const auto deadline = Clock::now() + ipc_timeout;
    while (Clock::now() < deadline) {
        const auto [signal, sender] = timed_signal(signal_set(false, true),
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()));
        if (signal == SIGINT || signal == SIGTERM) { throw Error("Connection cancelled by signal"); }
        if (signal == SIGUSR2 && sender == host_pid) { return sender; }
    }
    throw Error("Handshake timed out after 5 seconds (SIGUSR2)");
}
Fd::~Fd() { if (fd_ >= 0) { ::close(fd_); } }
Fd& Fd::operator=(Fd&& other) noexcept {
    if (this != &other) {
        if (fd_ >= 0) { ::close(fd_); }
        fd_ = std::exchange(other.fd_, -1);
    }
    return *this;
}
NamedSemaphore::NamedSemaphore(std::string name, bool create, unsigned value)
    : name_(std::move(name)), owner_(create) {
    sem_ = sem_open(name_.c_str(), create ? O_CREAT | O_EXCL : 0, 0600, value);
    if (sem_ == SEM_FAILED) { system_error("sem_open(" + name_ + ")"); }
}
NamedSemaphore::~NamedSemaphore() {
    if (sem_ != SEM_FAILED) { sem_close(sem_); }
    if (owner_) { sem_unlink(name_.c_str()); }
}
void NamedSemaphore::wait() {
    timespec deadline{};
    if (clock_gettime(CLOCK_REALTIME, &deadline) < 0) { system_error("clock_gettime"); }
    deadline.tv_sec += ipc_timeout.count();
    while (sem_timedwait(sem_, &deadline) < 0) {
        if (errno == EINTR) { continue; }
        if (errno == ETIMEDOUT) { throw Error("Semaphore timed out after 5 seconds: " + name_); }
        system_error("sem_timedwait(" + name_ + ")");
    }
}
void NamedSemaphore::post() { if (sem_post(sem_) < 0) { system_error("sem_post(" + name_ + ")"); } }
ChannelResources::ChannelResources(const std::string& root, int pid, bool create)
    : directory(root + "/c_" + std::to_string(pid)), owner(create),
      to_host(semaphore_prefix(root, pid) + "h", create),
      to_client(semaphore_prefix(root, pid) + "c", create) {
    if (create) { make_directory(directory); } else { check_directory(directory); }
}
ChannelResources::~ChannelResources() {
    if (owner) {
        std::error_code ignored;
        std::filesystem::remove_all(directory, ignored);
    }
}
void make_directory(const std::string& path) {
    if (mkdir(path.c_str(), 0700) < 0) { system_error("mkdir(" + path + ")"); }
}
void check_directory(const std::string& path) {
    struct stat info{};
    if (lstat(path.c_str(), &info) < 0) { system_error("Host resources unavailable: " + path); }
    if (!S_ISDIR(info.st_mode) || info.st_uid != getuid() || (info.st_mode & 0077)) {
        throw Error("Unsafe IPC directory: " + path);
    }
}
bool wait_fd(int fd, short events, std::chrono::milliseconds timeout) {
    const auto deadline = Clock::now() + timeout;
    do {
        const auto remaining = std::max<std::int64_t>(0,
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count());
        pollfd item{fd, events, 0};
        const int result = poll(&item, 1, static_cast<int>(remaining));
        if (result < 0 && errno == EINTR) { continue; }
        if (result < 0) { system_error("poll"); }
        if (result == 0) { return false; }
        if (item.revents & events) { return true; }
        if (item.revents & (POLLERR | POLLHUP | POLLNVAL)) { throw Error("Peer closed the channel"); }
    } while (Clock::now() < deadline);
    return false;
}
FramedConnection::FramedConnection(std::shared_ptr<ChannelResources> resources,
                                  Fd input, Fd output, bool host)
    : resources_(std::move(resources)), input_(std::move(input)), output_(std::move(output)),
      incoming_(host ? resources_->to_host : resources_->to_client),
      outgoing_(host ? resources_->to_client : resources_->to_host) {}
int FramedConnection::output_fd() const { return output_.get() < 0 ? input_.get() : output_.get(); }
void FramedConnection::transfer(int fd, std::uint8_t* data, std::size_t size, bool writing, Clock::time_point deadline) {
    while (size) {
        if (closed_) { throw Error("Connection closed"); }
        if (Clock::now() >= deadline) { throw Error("Packet transfer timed out after 5 seconds"); }
        if (!wait_fd(fd, writing ? POLLOUT : POLLIN, std::chrono::milliseconds(100))) { continue; }
        const auto count = writing ? ::write(fd, data, size) : ::read(fd, data, size);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) { continue; }
        if (count < 0) { system_error(writing ? "write" : "read"); }
        if (count == 0) { throw Error("Peer closed the channel"); }
        data += count;
        size -= static_cast<std::size_t>(count);
    }
}
void FramedConnection::send(std::span<const std::uint8_t> packet) {
    std::lock_guard lock(send_mutex_);
    if (closed_) { throw Error("Connection closed"); }
    if (packet.empty() || packet.size() > max_packet) { throw Error("Invalid packet size"); }
    outgoing_.space.wait();
    Bytes frame(4 + packet.size());
    const auto size = static_cast<std::uint32_t>(packet.size());
    for (int i = 0; i < 4; ++i) { frame[i] = static_cast<std::uint8_t>(size >> (24 - i * 8)); }
    std::copy(packet.begin(), packet.end(), frame.begin() + 4);
    try {
        transfer(output_fd(), frame.data(), frame.size(), true, Clock::now() + ipc_timeout);
        outgoing_.ready.post();
    } catch (...) { closed_ = true; throw; }
}
std::optional<Bytes> FramedConnection::receive(std::chrono::milliseconds idle_wait) {
    if (closed_) { throw Error("Connection closed"); }
    if (!resources_->owner && access(resources_->directory.c_str(), F_OK) < 0) {
        throw Error("Host removed the personal channel");
    }
    if (!wait_fd(input_.get(), POLLIN, idle_wait)) { return std::nullopt; }
    try {
        incoming_.ready.wait();
        const auto deadline = Clock::now() + ipc_timeout;
        std::uint8_t header[4];
        transfer(input_.get(), header, 4, false, deadline);
        std::uint32_t size = 0;
        for (auto byte : header) { size = (size << 8) | byte; }
        if (size == 0 || size > max_packet) { throw Error("Invalid frame length"); }
        Bytes packet(size);
        transfer(input_.get(), packet.data(), packet.size(), false, deadline);
        incoming_.space.post();
        return packet;
    } catch (...) { closed_ = true; throw; }
}
} // namespace application
