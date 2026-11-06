#include "common.hpp"

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iostream>
#include <sys/mman.h>

SharedSync* CreateSharedSync() {
    void* mem = mmap(nullptr,
                     sizeof(SharedSync),
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS,
                     -1,
                     0);
    if (mem == MAP_FAILED) {
        std::perror("mmap");
        return nullptr;
    }

    auto* sync = static_cast<SharedSync*>(mem);
    if (sem_init(&sync->io_lock, 1, 1) == -1) {
        std::perror("sem_init");
        munmap(sync, sizeof(SharedSync));
        return nullptr;
    }
    return sync;
}

void DestroySharedSync(SharedSync* sync) {
    if (sync == nullptr) {
        return;
    }
    sem_destroy(&sync->io_lock);
    munmap(sync, sizeof(SharedSync));
}

bool TimedLock(sem_t* sem, int timeout_sec) {
    timespec ts{};
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        return false;
    }
    ts.tv_sec += timeout_sec;

    while (sem_timedwait(sem, &ts) == -1) {
        if (errno == EINTR) {
            continue;
        }
        if (errno == ETIMEDOUT) {
            std::cerr << "Semaphore wait timed out after " << timeout_sec << " seconds\n";
        } else {
            std::perror("sem_timedwait");
        }
        return false;
    }
    return true;
}

void Unlock(sem_t* sem) {
    if (sem_post(sem) == -1) {
        std::perror("sem_post");
    }
}

ChatPacket MakePacket(MessageKind kind,
                      std::uint64_t sequence,
                      const std::string& sender,
                      const std::string& text) {
    ChatPacket packet{};
    packet.kind = static_cast<std::uint32_t>(kind);
    packet.sequence = sequence;
    std::snprintf(packet.sender, sizeof(packet.sender), "%s", sender.c_str());
    std::snprintf(packet.text, sizeof(packet.text), "%s", text.c_str());
    return packet;
}

std::string PacketText(const ChatPacket& packet) {
    return std::string(packet.text, strnlen(packet.text, sizeof(packet.text)));
}

std::string PacketSender(const ChatPacket& packet) {
    return std::string(packet.sender, strnlen(packet.sender, sizeof(packet.sender)));
}

std::string KindName(MessageKind kind) {
    switch (kind) {
        case MessageKind::Public:
            return "PUBLIC";
        case MessageKind::Private:
            return "PRIVATE";
        case MessageKind::System:
            return "SYSTEM";
    }
    return "UNKNOWN";
}
