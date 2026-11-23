#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>

#include <cerrno>
#include <cstring>

#include "conn.h"
#include "channel.h"
#include "logger.h"

using namespace channel;

const bool Conn::kCreateBeforeFork = false;

struct Conn::Impl {
    SharedChannel* ch = nullptr;
    bool host = false;
    char name[64] = {0};
};

Conn::Conn(pid_t id, bool create) : impl_(new Impl) {
    impl_->host = create;
    snprintf(impl_->name, sizeof(impl_->name), "/wolf_game_%d", static_cast<int>(id));
    const char* name = impl_->name;
    int fd = -1;

    if (create) {
        fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
        if (fd < 0 && errno == EEXIST) {
            shm_unlink(name);
            fd = shm_open(name, O_CREAT | O_EXCL | O_RDWR, 0600);
        }
        if (fd < 0) {
            Log("conn_shm", "shm_open(create) %s: %s", name, strerror(errno));
            return;
        }
        if (ftruncate(fd, sizeof(SharedChannel)) != 0) {
            Log("conn_shm", "ftruncate: %s", strerror(errno));
            close(fd);
            shm_unlink(name);
            return;
        }
    } else {

        long long start = NowMs();
        while ((fd = shm_open(name, O_RDWR, 0600)) < 0) {
            if (errno != ENOENT || NowMs() - start > kTimeoutSec * 1000) {
                Log("conn_shm", "shm_open(open) %s: %s", name, strerror(errno));
                return;
            }
            usleep(10 * 1000);
        }

        struct stat st;
        while (fstat(fd, &st) == 0 && static_cast<size_t>(st.st_size) < sizeof(SharedChannel)) {
            if (NowMs() - start > kTimeoutSec * 1000) {
                Log("conn_shm", "таймаут ожидания размера сегмента");
                close(fd);
                return;
            }
            usleep(1000);
        }
    }

    void* p = mmap(nullptr, sizeof(SharedChannel), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    close(fd);
    if (p == MAP_FAILED) {
        Log("conn_shm", "mmap: %s", strerror(errno));
        if (create) shm_unlink(name);
        return;
    }
    SharedChannel* ch = static_cast<SharedChannel*>(p);

    if (create) {
        if (!ChannelInit(ch)) {
            Log("conn_shm", "sem_init: %s", strerror(errno));
            munmap(p, sizeof(SharedChannel));
            shm_unlink(name);
            return;
        }
    } else {

        long long start = NowMs();
        while (!__atomic_load_n(&ch->ready, __ATOMIC_ACQUIRE)) {
            if (NowMs() - start > kTimeoutSec * 1000) {
                Log("conn_shm", "таймаут ожидания инициализации сегмента хостом");
                munmap(p, sizeof(SharedChannel));
                return;
            }
            usleep(1000);
        }
    }
    impl_->ch = ch;
    Log("conn_shm", "сегмент %s %s", name, create ? "создан" : "открыт");
}

Conn::~Conn() {
    if (impl_->ch) {
        if (impl_->host) ChannelDestroy(impl_->ch);
        munmap(impl_->ch, sizeof(SharedChannel));
    }
    if (impl_->host) shm_unlink(impl_->name);
    delete impl_;
}

bool Conn::Ok() const { return impl_->ch != nullptr; }

bool Conn::Read(void* buf, size_t count) {
    if (!impl_->ch) return false;
    if (!ChannelRead(impl_->ch, ReadSlot(impl_->host), buf, count)) {
        Log("conn_shm", "Read: таймаут или неверный размер сообщения");
        return false;
    }
    return true;
}

bool Conn::Write(const void* buf, size_t count) {
    if (!impl_->ch) return false;
    if (!ChannelWrite(impl_->ch, WriteSlot(impl_->host), buf, count)) {
        Log("conn_shm", "Write: таймаут ожидания свободного слота");
        return false;
    }
    return true;
}
