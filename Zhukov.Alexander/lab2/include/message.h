#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <QMetaType>

using Bytes = std::vector<char>;

constexpr std::size_t max_text = 1024;
constexpr std::size_t header_size = 36;
constexpr std::size_t max_packet = header_size + max_text;

enum class MessageKind : unsigned char { Public = 1, Private = 2, System = 3 };

struct Message {
    MessageKind kind = MessageKind::Public;
    int sender = 0;
    int recipient = -1;
    std::uint64_t sent_at = 0;
    std::uint64_t number = 0;
    std::string text;
    bool operator==(const Message& other) const {
        return kind == other.kind && sender == other.sender && recipient == other.recipient &&
               sent_at == other.sent_at && number == other.number && text == other.text;
    }
};

using History = std::vector<Message>;
Q_DECLARE_METATYPE(Message)
Q_DECLARE_METATYPE(History)

Bytes encode(const Message& message);
Message decode(const Bytes& bytes);
