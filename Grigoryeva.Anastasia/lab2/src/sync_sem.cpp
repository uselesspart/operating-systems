#include "sync_sem.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <time.h>

#include <cerrno>
#include <cstring>
#include <stdexcept>

std::string ConnName(const ConnId& id) {
    return "wolf_" + std::to_string(static_cast<long>(id.hostPid)) + "_" + std::to_string(id.id);
}

SemChannel::SemChannel(const ConnId& id, bool create) : owner_(create) {
    const std::string h2c = "/" + ConnName(id) + "_h2c";
    const std::string c2h = "/" + ConnName(id) + "_c2h";
    nameOut_ = create ? h2c : c2h;
    nameIn_ = create ? c2h : h2c;

    auto cleanup = [this]() {
        if (in_ != SEM_FAILED) sem_close(in_);
        if (out_ != SEM_FAILED) sem_close(out_);
        in_ = out_ = SEM_FAILED;
        if (owner_) {
            sem_unlink(nameIn_.c_str());
            sem_unlink(nameOut_.c_str());
        }
    };

    auto open = [&](const std::string& name) -> sem_t* {
        sem_t* s;
        if (create) {
            sem_unlink(name.c_str());  // остатки прошлого аварийного запуска
            s = sem_open(name.c_str(), O_CREAT | O_EXCL, 0600, 0);
        } else {
            s = sem_open(name.c_str(), 0);
        }
        if (s == SEM_FAILED) {
            std::string msg = "sem_open(" + name + "): " + std::strerror(errno);
            cleanup();
            throw std::runtime_error(msg);
        }
        return s;
    };

    out_ = open(nameOut_);
    in_ = open(nameIn_);
}

SemChannel::~SemChannel() {
    if (in_ != SEM_FAILED) sem_close(in_);
    if (out_ != SEM_FAILED) sem_close(out_);
    if (owner_) {
        sem_unlink(nameIn_.c_str());
        sem_unlink(nameOut_.c_str());
    }
}

void SemChannel::PostOutgoing() { sem_post(out_); }

bool SemChannel::WaitIncoming(int timeoutMs, std::string& err) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += timeoutMs / 1000;
    ts.tv_nsec += static_cast<long>(timeoutMs % 1000) * 1000000L;
    if (ts.tv_nsec >= 1000000000L) {
        ts.tv_sec += 1;
        ts.tv_nsec -= 1000000000L;
    }
    while (sem_timedwait(in_, &ts) != 0) {
        if (errno == EINTR) continue;
        if (errno == ETIMEDOUT) {
            err = "таймаут семафора (" + std::to_string(timeoutMs) + " мс)";
        } else {
            err = std::string("sem_timedwait: ") + std::strerror(errno);
        }
        return false;
    }
    return true;
}
