#ifndef CHAT_CHANNEL_H
#define CHAT_CHANNEL_H

#include "conn/conn.h"
#include "ipc/Semaphore.h"
#include "util/Bytes.h"

#include <chrono>
#include <cstddef>

namespace chat {

enum class ChannelStatus { Ok, Timeout, Closed, Corrupted };

const char* describe(ChannelStatus status);

// Framed exchange over a Conn. Turn order is kept by two global semaphores:
// the sender writes a whole frame and then posts the receiver's semaphore,
// the receiver waits on it (with a timeout) before reading
class Channel {
public:
    enum class Side { Host, Client };

    // Frames must fit into a pipe buffer (64 KiB) so that a writer never blocks before posting
    static constexpr std::size_t kMaxFrameSize = 60 * 1024;

    Channel(const ConnId& id, Side side);

    Channel(const Channel&) = delete;
    Channel& operator=(const Channel&) = delete;

    ChannelStatus send(const Bytes& payload);
    ChannelStatus receive(Bytes& payload, std::chrono::milliseconds timeout);

private:
    NamedSemaphore toHost_;
    NamedSemaphore toClient_;
    Conn conn_;
    NamedSemaphore& incoming_;
    NamedSemaphore& outgoing_;
};

}

#endif // CHAT_CHANNEL_H
