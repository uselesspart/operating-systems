#include "channel.h"

#include <cerrno>
#include <cstring>
#include <ctime>

namespace channel {

long long NowMs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

bool SemWaitTimeout(sem_t* s, int sec) {
    timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += sec;
    while (sem_timedwait(s, &ts) != 0) {
        if (errno == EINTR) continue;
        return false;
    }
    return true;
}

bool ChannelInit(SharedChannel* ch) {
    for (int i = 0; i < 2; ++i) {
        if (sem_init(&ch->full[i], 1, 0) != 0) return false;
        if (sem_init(&ch->freeSlot[i], 1, 1) != 0) return false;
        ch->len[i] = 0;
    }
    __atomic_store_n(&ch->ready, 1, __ATOMIC_RELEASE);
    return true;
}

void ChannelDestroy(SharedChannel* ch) {
    for (int i = 0; i < 2; ++i) {
        sem_destroy(&ch->full[i]);
        sem_destroy(&ch->freeSlot[i]);
    }
}

bool ChannelWrite(SharedChannel* ch, int slot, const void* buf, size_t count) {
    if (count > kMaxMsg) return false;
    if (!SemWaitTimeout(&ch->freeSlot[slot])) return false;
    memcpy(ch->data[slot], buf, count);
    ch->len[slot] = count;
    sem_post(&ch->full[slot]);
    return true;
}

bool ChannelRead(SharedChannel* ch, int slot, void* buf, size_t count) {
    if (!SemWaitTimeout(&ch->full[slot])) return false;
    const bool ok = (ch->len[slot] == count);
    if (ok) memcpy(buf, ch->data[slot], count);
    sem_post(&ch->freeSlot[slot]);
    return ok;
}

}
