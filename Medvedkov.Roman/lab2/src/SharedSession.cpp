#include "SharedSession.hpp"

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <new>
#include <semaphore.h>
#include <stdexcept>
#include <sys/mman.h>
#include <system_error>
#include <time.h>
#include <unistd.h>

namespace chat {
struct SharedSession::Data {
    sem_t gates[7];
    std::uint64_t next = 1;
    std::int64_t lastTimestamp = 0;
    std::int64_t clientActivity = 0;
};

namespace {
void check(int result, const char* operation) {
    if (result < 0) throw std::system_error(errno, std::generic_category(), operation);
}
}

SharedSession::SharedSession() : data_(nullptr), owner_(getpid()) {
    void* memory = mmap(nullptr, sizeof(Data), PROT_READ | PROT_WRITE,
                        MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (memory == MAP_FAILED) throw std::system_error(errno, std::generic_category(), "mmap semaphores");
    data_ = new (memory) Data{};
    const unsigned initial[] = {0, 0, 1, 1, 0, 1, 0};
    unsigned count = 0;
    try {
        for (; count < 7; ++count) check(sem_init(&data_->gates[count], 1, initial[count]), "sem_init");
        data_->clientActivity = monotonicNs();
    } catch (...) {
        for (unsigned i = 0; i < count; ++i) sem_destroy(&data_->gates[i]);
        munmap(data_, sizeof(Data));
        throw;
    }
}

SharedSession::~SharedSession() {
    // The parent destroys semaphores only after waiting for its child.
    if (getpid() == owner_) for (auto& gate : data_->gates) sem_destroy(&gate);
    munmap(data_, sizeof(Data));
}

void SharedSession::wait(Gate gate) {
    timespec limit{};
    check(clock_gettime(CLOCK_REALTIME, &limit), "clock_gettime");
    limit.tv_sec += TimeoutMs / 1000;
    while (sem_timedwait(&data_->gates[static_cast<int>(gate)], &limit) < 0) {
        if (errno == EINTR) continue;
        if (errno == ETIMEDOUT) throw std::runtime_error("Semaphore timeout: 5 seconds");
        check(-1, "sem_timedwait");
    }
}

bool SharedSession::tryWait(Gate gate) {
    while (sem_trywait(&data_->gates[static_cast<int>(gate)]) < 0) {
        if (errno == EINTR) continue;
        if (errno == EAGAIN) return false;
        check(-1, "sem_trywait");
    }
    return true;
}

void SharedSession::post(Gate gate) {
    check(sem_post(&data_->gates[static_cast<int>(gate)]), "sem_post");
}

SharedSession::Stamp SharedSession::reserve(Party sender) {
    if (sender != Party::Host && sender != Party::Client) throw std::invalid_argument("Invalid sender");
    wait(Gate::Order);
    try {
        timespec time{};
        check(clock_gettime(CLOCK_REALTIME, &time), "clock_gettime");
        const auto now = static_cast<std::int64_t>(time.tv_sec) * 1000000000 + time.tv_nsec;
        const auto monotonic = monotonicNs();
        data_->lastTimestamp = std::max(now, data_->lastTimestamp + 1);
        const Stamp stamp{data_->next++, data_->lastTimestamp};
        if (sender == Party::Client) data_->clientActivity = monotonic;
        post(Gate::Order);
        return stamp;
    } catch (...) {
        post(Gate::Order);
        throw;
    }
}

void SharedSession::startActivityClock() {
    const auto now = monotonicNs();
    wait(Gate::Order);
    data_->clientActivity = now;
    post(Gate::Order);
}

bool SharedSession::clientIdle(std::int64_t nowNs) {
    wait(Gate::Order);
    const auto last = data_->clientActivity;
    post(Gate::Order);
    return nowNs - last > 60000000000LL;
}

std::int64_t SharedSession::monotonicNs() {
    timespec time{};
    check(clock_gettime(CLOCK_MONOTONIC, &time), "clock_gettime");
    return static_cast<std::int64_t>(time.tv_sec) * 1000000000 + time.tv_nsec;
}
}
