#include "chat_session.h"

#include <algorithm>
#include <utility>

namespace chat {

    ChatSession::ChatSession(TimePoint started_at)
        : last_client_activity_(started_at) {}

    bool ChatSession::add_message(
        ChatMessage message,
        TimePoint received_at
    ) {
        // Use the existing protocol validation rules.
        if (!serialize(message).has_value()) {
            return false;
        }

        // Keep messages ordered by their sending time.
        // For equal timestamps, preserve insertion order.
        const auto position = std::upper_bound(
            history_.begin(),
            history_.end(),
            message.sent_at_ms,
            [](std::uint64_t timestamp, const ChatMessage& current) {
                return timestamp < current.sent_at_ms;
            }
        );

        const bool from_client =
            message.sender == Participant::Client;

        history_.insert(position, std::move(message));

        // Host messages must not reset the client's timeout.
        if (from_client && received_at > last_client_activity_) {
            last_client_activity_ = received_at;
        }

        return true;
    }

    bool ChatSession::client_timed_out(
        TimePoint now
    ) const noexcept {
        return now - last_client_activity_ >= ClientTimeout;
    }

    const std::vector<ChatMessage>&
    ChatSession::history() const noexcept {
        return history_;
    }

    ChatSession::TimePoint
    ChatSession::last_client_activity() const noexcept {
        return last_client_activity_;
    }

} // namespace chat