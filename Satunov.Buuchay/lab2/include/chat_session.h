#pragma once

#include "chat_message.h"

#include <chrono>
#include <vector>

namespace chat {

    class ChatSession {
    public:
        using Clock = std::chrono::steady_clock;
        using TimePoint = Clock::time_point;

        static constexpr auto ClientTimeout =
            std::chrono::seconds(60);

        explicit ChatSession(TimePoint started_at);

        [[nodiscard]] bool add_message(
            ChatMessage message,
            TimePoint received_at
        );

        [[nodiscard]] bool client_timed_out(
            TimePoint now
        ) const noexcept;

        [[nodiscard]] const std::vector<ChatMessage>&
        history() const noexcept;

        [[nodiscard]] TimePoint
        last_client_activity() const noexcept;

    private:
        std::vector<ChatMessage> history_;
        TimePoint last_client_activity_;
    };

} // namespace chat