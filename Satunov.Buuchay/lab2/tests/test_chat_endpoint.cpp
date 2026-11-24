#include "chat_endpoint.h"
#include "process_manager.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

namespace {

using chat::ChatEndpoint;
using chat::MessageKind;
using chat::Participant;

void require(bool condition, std::string_view description) {
    if (!condition) {
        throw std::runtime_error(std::string(description));
    }
}

class MemoryConnection final : public Conn {
public:
    using Queue = std::deque<std::vector<std::uint8_t>>;

    MemoryConnection(Queue& incoming, Queue& outgoing)
        : incoming_(incoming), outgoing_(outgoing) {}

    [[nodiscard]] bool read(
        std::vector<std::uint8_t>& frame
    ) override {
        if (incoming_.empty()) {
            return false;
        }

        frame = std::move(incoming_.front());
        incoming_.pop_front();

        return true;
    }

    [[nodiscard]] bool write(
        std::span<const std::uint8_t> frame
    ) override {
        if (!writable_) {
            return false;
        }

        outgoing_.emplace_back(frame.begin(), frame.end());
        return true;
    }

    void set_writable(bool value) {
        writable_ = value;
    }

private:
    Queue& incoming_;
    Queue& outgoing_;
    bool writable_{true};
};

void test_broadcast_and_private_messages() {
    MemoryConnection::Queue host_to_client;
    MemoryConnection::Queue client_to_host;

    MemoryConnection host_connection(
        client_to_host, host_to_client
    );

    MemoryConnection client_connection(
        host_to_client, client_to_host
    );

    ChatEndpoint host(
        host_connection, Participant::Host
    );

    ChatEndpoint client(
        client_connection, Participant::Client
    );

    require(
        host.send(MessageKind::Broadcast, "Hello everyone"),
        "host sends broadcast"
    );

    const auto broadcast = client.receive();

    require(
        broadcast.has_value(),
        "client receives broadcast"
    );

    require(
        broadcast->sender == Participant::Host &&
        broadcast->recipient == Participant::All &&
        broadcast->text == "Hello everyone",
        "broadcast fields are correct"
    );

    require(
        client.send(MessageKind::Private, "Hello host"),
        "client sends private message"
    );

    const auto reply = host.receive();

    require(
        reply.has_value(),
        "host receives private message"
    );

    require(
        reply->sender == Participant::Client &&
        reply->recipient == Participant::Host &&
        reply->text == "Hello host",
        "private message fields are correct"
    );

    const auto host_history = host.history();
    const auto client_history = client.history();

    require(
        host_history.size() == 2 &&
        client_history.size() == 2,
        "both endpoints have complete history"
    );

    require(
        host_history[0].sent_at_ms <=
            host_history[1].sent_at_ms,
        "history is chronologically ordered"
    );
}

void test_empty_and_failed_messages() {
    MemoryConnection::Queue host_to_client;
    MemoryConnection::Queue client_to_host;

    MemoryConnection connection(
        client_to_host, host_to_client
    );

    ChatEndpoint host(connection, Participant::Host);

    require(
        !host.send(MessageKind::Broadcast, ""),
        "empty message rejected"
    );

    connection.set_writable(false);

    require(
        !host.send(MessageKind::Broadcast, "Failed"),
        "transport write failure propagated"
    );

    require(
        host.history().empty(),
        "failed messages are not stored"
    );

    require(
        !host.receive().has_value(),
        "empty inbox returns no message"
    );
}

void test_invalid_incoming_message() {
    MemoryConnection::Queue host_to_client;
    MemoryConnection::Queue client_to_host;

    MemoryConnection connection(
        host_to_client, client_to_host
    );

    ChatEndpoint client(connection, Participant::Client);

    host_to_client.push_back({1, 2, 3});

    bool invalid_rejected = false;

    try {
        (void)client.receive();
    } catch (const std::runtime_error&) {
        invalid_rejected = true;
    }

    require(
        invalid_rejected,
        "corrupted packet rejected"
    );

    const chat::ChatMessage forged{
        MessageKind::Broadcast,
        Participant::Client,
        Participant::All,
        123,
        "Wrong sender"
    };

    const auto bytes = chat::serialize(forged);
    require(bytes.has_value(), "test packet serialized");

    host_to_client.push_back(*bytes);

    bool sender_rejected = false;

    try {
        (void)client.receive();
    } catch (const std::runtime_error&) {
        sender_rejected = true;
    }

    require(
        sender_rejected,
        "unexpected sender rejected"
    );

    require(
        client.history().empty(),
        "invalid messages not stored"
    );
}

void test_inactivity() {
    MemoryConnection::Queue host_to_client;
    MemoryConnection::Queue client_to_host;

    MemoryConnection host_connection(
        client_to_host, host_to_client
    );

    MemoryConnection client_connection(
        host_to_client, client_to_host
    );

    const auto start = ChatEndpoint::Clock::now() - 61s;

    ChatEndpoint host(
        host_connection, Participant::Host, start
    );

    ChatEndpoint client(
        client_connection, Participant::Client, start
    );

    require(
        host.client_timed_out(),
        "host detects inactive client"
    );

    require(
        host.send(MessageKind::Broadcast, "Host activity"),
        "host sends message"
    );

    require(
        host.client_timed_out(),
        "host messages do not reset client timeout"
    );

    require(
        client.send(MessageKind::Private, "I'm here"),
        "client sends message"
    );

    require(
        host.receive().has_value(),
        "host receives client activity"
    );

    require(
        !host.client_timed_out(),
        "client activity resets timeout"
    );

    require(
        !client.client_timed_out(),
        "client does not manage host inactivity"
    );
}

    void test_semaphore_message_ordering() {
    SharedSemaphores sync;

    MemoryConnection::Queue host_to_client;
    MemoryConnection::Queue client_to_host;

    MemoryConnection host_connection(
        client_to_host, host_to_client
    );

    MemoryConnection client_connection(
        host_to_client, client_to_host
    );

    ChatEndpoint host(
        host_connection,
        Participant::Host,
        ChatEndpoint::Clock::now(),
        &sync
    );

    ChatEndpoint client(
        client_connection,
        Participant::Client,
        ChatEndpoint::Clock::now(),
        &sync
    );

    require(
        host.send(MessageKind::Broadcast, "First"),
        "host sends first message"
    );

    require(
        client.receive().has_value(),
        "client receives first message"
    );

    require(
        host.send(MessageKind::Broadcast, "Second"),
        "host sends second message after acknowledgment"
    );

    require(
        client.receive().has_value(),
        "client receives second message"
    );

    require(
        client.send(MessageKind::Private, "First reply"),
        "client sends first reply"
    );

    require(
        host.receive().has_value(),
        "host receives first reply"
    );

    require(
        client.send(MessageKind::Private, "Second reply"),
        "client sends second reply after acknowledgment"
    );

    require(
        host.receive().has_value(),
        "host receives second reply"
    );

    require(
        host.history().size() == 4,
        "host stores all four messages"
    );

    require(
        client.history().size() == 4,
        "client stores all four messages"
    );
}

} // namespace

int main() {
    try {
        test_broadcast_and_private_messages();
        test_empty_and_failed_messages();
        test_invalid_incoming_message();
        test_inactivity();
        test_semaphore_message_ordering();

        std::cout << "Chat endpoint tests passed\n";
        return 0;

    } catch (const std::exception& error) {
        std::cerr << "Chat endpoint tests failed: "
                  << error.what() << '\n';
        return 1;
    }
}