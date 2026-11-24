#include "chat_message.h"

#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

using chat::ChatMessage;
using chat::MessageKind;
using chat::Participant;

void require(bool ok, std::string_view description) {
    if (!ok) {
        throw std::runtime_error(std::string(description));
    }
}

void test_round_trip() {
    const ChatMessage broadcast{
        MessageKind::Broadcast,
        Participant::Host,
        Participant::All,
        123456789,
        "Hello!"
    };

    const auto bytes = chat::serialize(broadcast);
    require(bytes.has_value(), "broadcast serialization");

    const auto decoded = chat::deserialize(*bytes);
    require(decoded.has_value() && *decoded == broadcast,
            "broadcast round trip");

    const ChatMessage private_message{
        MessageKind::Private,
        Participant::Client,
        Participant::Host,
        123456790,
        "Привет!"
    };

    const auto private_bytes = chat::serialize(private_message);
    require(private_bytes.has_value(), "private serialization");

    const auto decoded_private = chat::deserialize(*private_bytes);
    require(decoded_private.has_value() &&
            *decoded_private == private_message,
            "private round trip");
}

void test_reject_invalid_messages() {
    ChatMessage message{
        MessageKind::Broadcast,
        Participant::Host,
        Participant::All,
        1,
        "Hi"
    };

    message.text.clear();
    require(!chat::serialize(message), "empty message rejected");

    message.text.assign(chat::kMaxTextBytes + 1, 'x');
    require(!chat::serialize(message), "oversized message rejected");

    message.text = "Hi";
    message.sender = Participant::All;
    require(!chat::serialize(message), "invalid sender rejected");

    message.sender = Participant::Host;
    message.recipient = Participant::Client;
    require(!chat::serialize(message), "invalid broadcast recipient");

    message.kind = MessageKind::Private;
    message.recipient = Participant::Host;
    require(!chat::serialize(message), "private message to self");
}

void test_reject_corrupted_frames() {
    const ChatMessage message{
        MessageKind::Broadcast,
        Participant::Host,
        Participant::All,
        1,
        "test"
    };

    const auto serialized = chat::serialize(message);
    require(serialized.has_value(), "frame serialization");

    auto bytes = *serialized;

    bytes.pop_back();
    require(!chat::deserialize(bytes), "truncated frame rejected");

    bytes = *serialized;
    bytes.push_back(0);
    require(!chat::deserialize(bytes), "extra bytes rejected");

    bytes = *serialized;
    bytes[0] = 0;
    require(!chat::deserialize(bytes), "bad magic rejected");

    bytes = *serialized;
    bytes[4] = 99;
    require(!chat::deserialize(bytes), "unsupported version rejected");

    bytes = *serialized;
    bytes[5] = 99;
    require(!chat::deserialize(bytes), "unknown message kind rejected");

    bytes = *serialized;
    bytes[17] = 0;
    require(!chat::deserialize(bytes), "invalid payload length");
}

void test_maximum_length() {
    const ChatMessage message{
        MessageKind::Private,
        Participant::Host,
        Participant::Client,
        0,
        std::string(chat::kMaxTextBytes, 'x')
    };

    const auto bytes = chat::serialize(message);

    require(bytes.has_value() &&
            bytes->size() == chat::kHeaderBytes + chat::kMaxTextBytes,
            "max length serialization");

    const auto decoded = chat::deserialize(*bytes);

    require(decoded.has_value() && *decoded == message,
            "max length round trip");
}

int main() {
    try {
        test_round_trip();
        test_reject_invalid_messages();
        test_reject_corrupted_frames();
        test_maximum_length();

        std::cout << "All chat message tests passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Test failed: " << error.what() << '\n';
        return 1;
    }
}