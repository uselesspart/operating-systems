#pragma once

#include "chat_session.h"
#include "conn.h"

#include <mutex>
#include <optional>
#include <string>
#include <vector>

class SharedSemaphores;

namespace chat {

    class ChatEndpoint {
    public:
        using Clock = ChatSession::Clock;
        using TimePoint = ChatSession::TimePoint;

        ChatEndpoint(
            Conn& connection,
            Participant local,
            TimePoint started_at = Clock::now(),
            SharedSemaphores* sync = nullptr
        );

        [[nodiscard]] bool send(
            MessageKind kind,
            std::string text
        );

        // nullopt means that no message arrived during the
        // transport's read timeout.
        [[nodiscard]] std::optional<ChatMessage> receive();

        [[nodiscard]] std::vector<ChatMessage>
        history() const;

        [[nodiscard]] bool client_timed_out(
            TimePoint now = Clock::now()
        ) const;

    private:
        Conn& connection_;
        Participant local_;
        Participant peer_;

        mutable std::mutex mutex_;
        ChatSession session_;
        SharedSemaphores* sync_{nullptr};
    };

} // namespace chat