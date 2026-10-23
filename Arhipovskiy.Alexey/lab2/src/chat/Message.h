#ifndef CHAT_MESSAGE_H
#define CHAT_MESSAGE_H

#include "util/Clock.h"

#include <cstdint>
#include <string>

namespace chat {

constexpr int kHostId = 0;
constexpr int kEveryone = -1;

struct Participant {
    int id = 0;
    std::string name;

    bool operator==(const Participant& other) const { return id == other.id && name == other.name; }
};

// What a participant asks to send; the host turns it into a ChatMessage
struct OutgoingMessage {
    int to = kEveryone;
    Millis sentAtMs = 0;
    std::string text;
};

struct ChatMessage {
    std::uint64_t seq = 0;
    Millis sentAtMs = 0;
    int from = kHostId;
    int to = kEveryone;
    std::string fromName;
    std::string toName;
    std::string text;

    [[nodiscard]] bool isPrivate() const { return to != kEveryone; }
};

// Chat order: by the time of sending, ties broken by the host's arrival order
inline bool sentBefore(const ChatMessage& a, const ChatMessage& b)
{
    return a.sentAtMs != b.sentAtMs ? a.sentAtMs < b.sentAtMs : a.seq < b.seq;
}

}

#endif // CHAT_MESSAGE_H
