#include "message.h"

#include <QStringDecoder>
#include <stdexcept>

namespace {

void put(Bytes& bytes, std::uint64_t value, unsigned count) {
    for (unsigned i = 0; i < count; ++i) {
        bytes.push_back(static_cast<char>(value >> (i * 8)));
    }
}

std::uint64_t get(const Bytes& bytes, std::size_t offset, unsigned count) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < count; ++i) {
        value |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes[offset + i])) << (i * 8);
    }
    return value;
}

void validate(const Message& message) {
    auto kind = static_cast<unsigned>(message.kind);
    if (kind < 1 || kind > 3 || message.sender < 0 || message.sender > 1 ||
        message.recipient < -1 || message.recipient > 1 || message.text.size() > max_text ||
        (message.kind == MessageKind::Public && message.recipient != -1) ||
        (message.kind == MessageKind::Private && message.recipient < 0)) {
        throw std::runtime_error("Invalid message");
    }
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    QString text = decoder(QByteArrayView(message.text.data(), message.text.size()));
    if (decoder.hasError()) {
        throw std::runtime_error("Invalid UTF-8");
    }
}

}

Bytes encode(const Message& message) {
    validate(message);
    Bytes bytes;
    bytes.reserve(header_size + message.text.size());
    put(bytes, 0x324c534f, 4);
    put(bytes, 1, 1);
    put(bytes, static_cast<unsigned>(message.kind), 1);
    put(bytes, 0, 2);
    put(bytes, static_cast<std::uint32_t>(message.sender), 4);
    put(bytes, static_cast<std::uint32_t>(message.recipient), 4);
    put(bytes, message.sent_at, 8);
    put(bytes, message.number, 8);
    put(bytes, message.text.size(), 4);
    bytes.insert(bytes.end(), message.text.begin(), message.text.end());
    return bytes;
}

Message decode(const Bytes& bytes) {
    if (bytes.size() < header_size || bytes.size() > max_packet ||
        get(bytes, 0, 4) != 0x324c534f || get(bytes, 4, 1) != 1 || get(bytes, 6, 2) != 0 ||
        get(bytes, 32, 4) != bytes.size() - header_size) {
        throw std::runtime_error("Invalid packet");
    }
    Message message;
    message.kind = static_cast<MessageKind>(get(bytes, 5, 1));
    message.sender = static_cast<std::int32_t>(get(bytes, 8, 4));
    message.recipient = static_cast<std::int32_t>(get(bytes, 12, 4));
    message.sent_at = get(bytes, 16, 8);
    message.number = get(bytes, 24, 8);
    message.text.assign(bytes.data() + header_size, bytes.size() - header_size);
    validate(message);
    return message;
}
