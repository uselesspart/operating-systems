#pragma once

#include <semaphore.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <string>

constexpr int kSemaphoreTimeoutSec = 5;
constexpr int kClientIdleTimeoutSec = 60;
constexpr std::size_t kTextSize = 384;

enum class Role {
    Host,
    Client
};

enum class MessageKind : std::uint32_t {
    Public = 1,
    Private = 2,
    System = 3
};

struct ChatPacket {
    std::uint32_t kind;
    std::uint64_t sequence;
    char sender[32];
    char text[kTextSize];
};

static_assert(sizeof(ChatPacket) < 512, "ChatPacket must fit into one mq/FIFO write");

struct SharedSync {
    sem_t io_lock;
};

SharedSync* CreateSharedSync();
void DestroySharedSync(SharedSync* sync);

bool TimedLock(sem_t* sem, int timeout_sec = kSemaphoreTimeoutSec);
void Unlock(sem_t* sem);

ChatPacket MakePacket(MessageKind kind,
                      std::uint64_t sequence,
                      const std::string& sender,
                      const std::string& text);
std::string PacketText(const ChatPacket& packet);
std::string PacketSender(const ChatPacket& packet);
std::string KindName(MessageKind kind);
