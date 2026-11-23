#include <sys/mman.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

#include "conn.h"
#include "channel.h"
#include "logger.h"

using namespace channel;

const bool Conn::kCreateBeforeFork = true;

struct Conn::Impl {
    SharedChannel* ch = nullptr;
    pid_t owner = 0;
    bool IsHost() const { return getpid() == owner; }
};

Conn::Conn(pid_t id, bool create) : impl_(new Impl) {
    impl_->owner = getpid();
    void* p = mmap(nullptr, sizeof(SharedChannel), PROT_READ | PROT_WRITE,
                   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) {
        Log("conn_mmap", "mmap: %s", strerror(errno));
        return;
    }
    SharedChannel* ch = static_cast<SharedChannel*>(p);
    if (!ChannelInit(ch)) {
        Log("conn_mmap", "sem_init: %s", strerror(errno));
        munmap(p, sizeof(SharedChannel));
        return;
    }
    impl_->ch = ch;
    Log("conn_mmap", "anonymous mmap создан (%zu байт)", sizeof(SharedChannel));
}

Conn::~Conn() {
    if (impl_->ch) {
        if (impl_->IsHost()) ChannelDestroy(impl_->ch);
        munmap(impl_->ch, sizeof(SharedChannel));
    }
    delete impl_;
}

bool Conn::Ok() const { return impl_->ch != nullptr; }

bool Conn::Read(void* buf, size_t count) {
    if (!impl_->ch) return false;
    if (!ChannelRead(impl_->ch, ReadSlot(impl_->IsHost()), buf, count)) {
        Log("conn_mmap", "Read: таймаут или неверный размер сообщения");
        return false;
    }
    return true;
}

bool Conn::Write(const void* buf, size_t count) {
    if (!impl_->ch) return false;
    if (!ChannelWrite(impl_->ch, WriteSlot(impl_->IsHost()), buf, count)) {
        Log("conn_mmap", "Write: таймаут ожидания свободного слота");
        return false;
    }
    return true;
}
