#ifndef CHAT_CHAT_ROOM_H
#define CHAT_CHAT_ROOM_H

#include "chat/Limits.h"
#include "chat/Message.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace chat {

struct PostResult {
    std::optional<ChatMessage> message;
    std::string error;
    bool visibleToHost = false;
};

struct Delivery {
    std::vector<ChatMessage> messages;
    std::vector<std::string> notices;
};

// Chat state kept by the host: participants, routing of public and private messages,
// per-client delivery queues and inactivity tracking. Thread-safe
class ChatRoom {
public:
    using Clock = std::function<Millis()>;

    static constexpr const char* kHostName = "Хост";

    explicit ChatRoom(std::chrono::milliseconds idleLimit = kDefaultIdleLimit, Clock clock = wallClockMs);

    // Returns the name the client got; it is unique in the room
    std::string join(int id, const std::string& requestedName);
    void leave(int id, const std::string& reason);

    [[nodiscard]] bool contains(int id) const;
    [[nodiscard]] std::vector<Participant> participants() const;
    [[nodiscard]] std::uint64_t rosterVersion() const;

    PostResult post(int from, const OutgoingMessage& message);

    // Messages older than kOrderingDelay, in sending order, at most kMaxBatch; and pending notices.
    // The delay lets messages from slower clients arrive so nobody sees them out of order
    Delivery collect(int id);

    // No message from the client for longer than the idle limit
    [[nodiscard]] bool isIdle(int id) const;

private:
    struct Member {
        std::string name;
        Millis lastActivityMs = 0;
        std::vector<ChatMessage> pending;
        std::vector<std::string> notices;
    };

    [[nodiscard]] bool nameTaken(const std::string& name) const;
    [[nodiscard]] std::optional<std::string> nameOf(int id) const;
    void notifyOthers(int except, const std::string& notice);
    PostResult reject(int from, std::string error);

    const std::chrono::milliseconds idleLimit_;
    const Clock clock_;

    mutable std::mutex mutex_;
    std::map<int, Member> members_;
    std::uint64_t nextSeq_ = 1;
    std::uint64_t rosterVersion_ = 1;
};

}

#endif // CHAT_CHAT_ROOM_H
