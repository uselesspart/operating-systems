#pragma once

#include <cstddef>
#include <semaphore.h>

namespace channel {

constexpr size_t kMaxMsg = 64;
constexpr int kTimeoutSec = 5;

constexpr int kClientToHost = 0;
constexpr int kHostToClient = 1;
constexpr int ReadSlot(bool host) { return host ? kClientToHost : kHostToClient; }
constexpr int WriteSlot(bool host) { return host ? kHostToClient : kClientToHost; }

long long NowMs();

bool SemWaitTimeout(sem_t* s, int sec = kTimeoutSec);

struct SharedChannel {
    int ready;
    sem_t full[2];
    sem_t freeSlot[2];
    size_t len[2];
    char data[2][kMaxMsg];
};

bool ChannelInit(SharedChannel* ch);
void ChannelDestroy(SharedChannel* ch);
bool ChannelWrite(SharedChannel* ch, int slot, const void* buf, size_t count);
bool ChannelRead(SharedChannel* ch, int slot, void* buf, size_t count);

}
