#pragma once
// Локальный семафор (sem_init) с тайм-аутом 5 секунд по умолчанию.
#include <errno.h>
#include <semaphore.h>
#include <time.h>

class Sem {
public:
    explicit Sem(unsigned value = 1) { sem_init(&s_, 0, value); }
    ~Sem() { sem_destroy(&s_); }
    Sem(const Sem&) = delete;
    Sem& operator=(const Sem&) = delete;

    bool Lock(int timeout_ms = 5000) {
        timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += timeout_ms / 1000;
        ts.tv_nsec += static_cast<long>(timeout_ms % 1000) * 1000000L;
        if (ts.tv_nsec >= 1000000000L) {
            ts.tv_sec++;
            ts.tv_nsec -= 1000000000L;
        }
        while (sem_timedwait(&s_, &ts) != 0) {
            if (errno == EINTR) continue;
            return false;  // ETIMEDOUT или ошибка
        }
        return true;
    }
    void Unlock() { sem_post(&s_); }

private:
    sem_t s_;
};

class SemGuard {
public:
    explicit SemGuard(Sem& s, int timeout_ms = 5000) : s_(s), ok_(s.Lock(timeout_ms)) {}
    ~SemGuard() { if (ok_) s_.Unlock(); }
    SemGuard(const SemGuard&) = delete;
    SemGuard& operator=(const SemGuard&) = delete;
    bool ok() const { return ok_; }

private:
    Sem& s_;
    bool ok_;
};
