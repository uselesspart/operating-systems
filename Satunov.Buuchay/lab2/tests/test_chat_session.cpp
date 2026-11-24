#include "chat_session.h"

#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

using namespace std::chrono_literals;

namespace {

using chat::ChatMessage;
using chat::ChatSession;
using chat::MessageKind;
using chat::Participant;

void require(bool condition, std::string_view description) {
    if (!condition) {
        throw std::runtime_error(std::string(description));
    }
}

ChatMessage make_message(
    Participant sender,
    MessageKind kind,
    std::uint64_t timestamp,
    std::string text
) {
    const Participant recipient =
        kind == MessageKind::Broadcast
            ? Participant::All
            : sender == Participant::Host
                ? Participant::Client
                : Participant::Host;

    return {
        kind,
        sender,
        recipient,
        timestamp,
        std::move(text)
    };
}

void test_message_order() {
    const auto start = ChatSession::Clock::now();
    ChatSession session(start);

    require(
        session.add_message(
            make_message(
                Participant::Host,
                MessageKind::Broadcast,
                300,
                "Third"
            ),
            start
        ),
        "third message accepted"
    );

    require(
        session.add_message(
            make_message(
                Participant::Client,
                MessageKind::Private,
                100,
                "First"
            ),
            start + 1s
        ),
        "first message accepted"
    );

    require(
        session.add_message(
            make_message(
                Participant::Host,
                MessageKind::Broadcast,
                200,
                "Second"
            ),
            start + 2s
        ),
        "second message accepted"
    );

    const auto& history = session.history();

    require(history.size() == 3, "three stored messages");
    require(history[0].text == "First", "first by timestamp");
    require(history[1].text == "Second", "second by timestamp");
    require(history[2].text == "Third", "third by timestamp");
}

void test_equal_timestamps() {
    const auto start = ChatSession::Clock::now();
    ChatSession session(start);

    require(
        session.add_message(
            make_message(
                Participant::Host,
                MessageKind::Broadcast,
                100,
                "A"
            ),
            start
        ),
        "message A accepted"
    );

    require(
        session.add_message(
            make_message(
                Participant::Client,
                MessageKind::Broadcast,
                100,
                "B"
            ),
            start
        ),
        "message B accepted"
    );

    const auto& history = session.history();

    require(history[0].text == "A", "A remains first");
    require(history[1].text == "B", "B remains second");
}

void test_invalid_messages() {
    const auto start = ChatSession::Clock::now();
    ChatSession session(start);

    ChatMessage invalid{
        MessageKind::Broadcast,
        Participant::Client,
        Participant::Host,
        100,
        "Invalid broadcast"
    };

    require(
        !session.add_message(invalid, start + 10s),
        "invalid recipient rejected"
    );

    require(
        session.history().empty(),
        "invalid message not stored"
    );

    require(
        session.last_client_activity() == start,
        "invalid client message does not reset timeout"
    );
}

void test_client_inactivity() {
    const auto start = ChatSession::Clock::now();
    ChatSession session(start);

    require(
        !session.client_timed_out(start + 59s),
        "client active before timeout"
    );

    require(
        session.client_timed_out(start + 60s),
        "client timed out after 60 seconds"
    );

    require(
        session.add_message(
            make_message(
                Participant::Client,
                MessageKind::Private,
                100,
                "Client activity"
            ),
            start + 30s
        ),
        "client activity accepted"
    );

    require(
        !session.client_timed_out(start + 89s),
        "client timeout reset"
    );

    require(
        session.client_timed_out(start + 90s),
        "client expires 60 seconds after last message"
    );
}

void test_host_does_not_reset_timeout() {
    const auto start = ChatSession::Clock::now();
    ChatSession session(start);

    require(
        session.add_message(
            make_message(
                Participant::Host,
                MessageKind::Broadcast,
                100,
                "Host activity"
            ),
            start + 50s
        ),
        "host message accepted"
    );

    require(
        session.client_timed_out(start + 60s),
        "host message does not reset client timeout"
    );
}

} // namespace

int main() {
    try {
        test_message_order();
        test_equal_timestamps();
        test_invalid_messages();
        test_client_inactivity();
        test_host_does_not_reset_timeout();

        std::cout << "Chat session tests passed\n";
        return 0;

    } catch (const std::exception& error) {
        std::cerr << "Chat session tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}