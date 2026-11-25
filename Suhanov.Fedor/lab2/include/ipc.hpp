#pragma once
#include "Connection.hpp"
#include <atomic>
#include <mutex>
#include <semaphore.h>
#include <stdexcept>
#include <utility>

namespace application {
using Clock = std::chrono::steady_clock;
class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};
[[noreturn]] void system_error(const std::string& operation);
bool process_alive(int pid);
std::string root_path(int host_pid);
void prepare_signals();
// {signal number, sender PID}; {0, 0} on a planned timeout.
std::pair<int, int> wait_signal(bool introductions, std::chrono::milliseconds timeout);
int wait_for_ack(int host_pid);
class Fd {
    int fd_ = -1;
public:
    explicit Fd(int fd = -1) noexcept : fd_(fd) {}
    ~Fd();
    Fd(const Fd&) = delete;
    Fd& operator=(const Fd&) = delete;
    Fd(Fd&& other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    Fd& operator=(Fd&& other) noexcept;
    int get() const noexcept { return fd_; }
};
class NamedSemaphore {
    std::string name_;
    sem_t* sem_ = SEM_FAILED;
    bool owner_ = false;
public:
    NamedSemaphore(std::string name, bool create, unsigned value);
    ~NamedSemaphore();
    NamedSemaphore(const NamedSemaphore&) = delete;
    NamedSemaphore& operator=(const NamedSemaphore&) = delete;
    void wait();
    void post();
};
struct Direction {
    NamedSemaphore space;
    NamedSemaphore ready;
    Direction(const std::string& prefix, bool create)
        : space(prefix + "_s", create, 1), ready(prefix + "_r", create, 0) {}
};
struct ChannelResources {
    std::string directory;
    bool owner;
    Direction to_host;
    Direction to_client;
    ChannelResources(const std::string& root, int pid, bool create);
    ~ChannelResources();
};
class FramedConnection : public IConnection {
    std::shared_ptr<ChannelResources> resources_;
    Fd input_;
    Fd output_;
    Direction& incoming_;
    Direction& outgoing_;
    std::atomic<bool> closed_{false};
    std::mutex send_mutex_;
    int output_fd() const;
    void transfer(int fd, std::uint8_t* data, std::size_t size, bool writing, Clock::time_point deadline);
public:
    FramedConnection(std::shared_ptr<ChannelResources> resources, Fd input,
                     Fd output, bool host);
    void send(std::span<const std::uint8_t> packet) override;
    std::optional<Bytes> receive(std::chrono::milliseconds idle_wait) override;
    void close() noexcept override { closed_.store(true); }
};
void make_directory(const std::string& path);
void check_directory(const std::string& path);
bool wait_fd(int fd, short events, std::chrono::milliseconds timeout);
} // namespace application
