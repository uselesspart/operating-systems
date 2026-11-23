#include <signal.h>
#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "conn.h"
#include "channel.h"
#include "logger.h"

using namespace channel;

const bool Conn::kCreateBeforeFork = true;

namespace {
struct PipeSync {
    sem_t full[2];
};
}

struct Conn::Impl {
    int c2h[2] = {-1, -1};
    int h2c[2] = {-1, -1}; 
    PipeSync* sync = nullptr;
    pid_t owner = 0;
    bool prepared = false;

    bool IsHost() const { return getpid() == owner; }

    void Prepare() {
        if (prepared) return;
        prepared = true;
        if (IsHost()) {
            close(c2h[1]); c2h[1] = -1;
            close(h2c[0]); h2c[0] = -1;
        } else {
            close(c2h[0]); c2h[0] = -1;
            close(h2c[1]); h2c[1] = -1;
        }
    }
};

Conn::Conn(pid_t id, bool create) : impl_(new Impl) {
    impl_->owner = getpid();
    signal(SIGPIPE, SIG_IGN);

    if (pipe(impl_->c2h) != 0 || pipe(impl_->h2c) != 0) {
        Log("conn_pipe", "pipe: %s", strerror(errno));
        return;
    }
    void* p = mmap(nullptr, sizeof(PipeSync), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        Log("conn_pipe", "mmap: %s", strerror(errno));
        return;
    }
    PipeSync* s = static_cast<PipeSync*>(p);
    if (sem_init(&s->full[0], 1, 0) != 0 || sem_init(&s->full[1], 1, 0) != 0) {
        Log("conn_pipe", "sem_init: %s", strerror(errno));
        munmap(p, sizeof(PipeSync));
        return;
    }
    impl_->sync = s;
    Log("conn_pipe", "каналы созданы");
}

Conn::~Conn() {
    int* fds[4] = {&impl_->c2h[0], &impl_->c2h[1], &impl_->h2c[0], &impl_->h2c[1]};
    for (int* fd : fds)
        if (*fd >= 0) close(*fd);
    if (impl_->sync) {
        if (impl_->IsHost()) {
            sem_destroy(&impl_->sync->full[0]);
            sem_destroy(&impl_->sync->full[1]);
        }
        munmap(impl_->sync, sizeof(PipeSync));
    }
    delete impl_;
}

bool Conn::Ok() const { return impl_->sync != nullptr; }

bool Conn::Read(void* buf, size_t count) {
    if (!impl_->sync || count > kMaxMsg) return false;
    impl_->Prepare();
    const bool host = impl_->IsHost();
    if (!SemWaitTimeout(&impl_->sync->full[ReadSlot(host)])) {
        Log("conn_pipe", "Read: таймаут ожидания сообщения");
        return false;
    }
    const int fd = host ? impl_->c2h[0] : impl_->h2c[0];
    char* p = static_cast<char*>(buf);
    size_t got = 0;
    while (got < count) {
        ssize_t n = read(fd, p + got, count - got);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            Log("conn_pipe", "Read: %s", n == 0 ? "канал закрыт" : strerror(errno));
            return false;
        }
        got += static_cast<size_t>(n);
    }
    return true;
}

bool Conn::Write(const void* buf, size_t count) {
    if (!impl_->sync || count > kMaxMsg) return false;
    impl_->Prepare();
    const bool host = impl_->IsHost();
    const int fd = host ? impl_->h2c[1] : impl_->c2h[1];
    const char* p = static_cast<const char*>(buf);
    size_t sent = 0;
    while (sent < count) {
        ssize_t n = write(fd, p + sent, count - sent);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) {
            Log("conn_pipe", "Write: %s", strerror(errno));
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    sem_post(&impl_->sync->full[WriteSlot(host)]);
    return true;
}
