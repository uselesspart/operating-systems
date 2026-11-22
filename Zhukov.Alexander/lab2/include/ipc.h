#pragma once

#include <atomic>
#include <chrono>
#include <semaphore.h>
#include <string>
#include <stdexcept>

class ConnectionClosed final : public std::runtime_error {
public:
    ConnectionClosed() : std::runtime_error("Connection closed") {}
};

using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;

constexpr int timeout_ms = 5000;
constexpr int poll_ms = 100;

void check_stop(const std::atomic_bool& stop);
bool process_alive(int fd);
bool can_write(int fd);
std::string queue_name(const std::string& directory, const std::string& suffix);
int remaining_ms(Deadline deadline, int limit = poll_ms);
void system_error(const std::string& text);

class NamedSemaphore {
public:
    NamedSemaphore() = default;
    ~NamedSemaphore();
    NamedSemaphore(const NamedSemaphore&) = delete;
    NamedSemaphore& operator=(const NamedSemaphore&) = delete;

    void open(const std::string& name, bool create, unsigned value = 0);
    bool try_wait();
    void wait(Deadline deadline, const std::atomic_bool& stop, int peer_fd = -1);
    void post();
    void post_on_exit() noexcept;

private:
    sem_t* sem = SEM_FAILED;
    std::string name;
    bool owner = false;
};
