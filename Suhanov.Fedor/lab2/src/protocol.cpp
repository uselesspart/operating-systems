#include "protocol.hpp"
#include "ipc.hpp"
#include "events.hpp"
#include <cctype>

namespace application {
namespace {
void number(Bytes& bytes, std::uint64_t value, unsigned width) {
    for (unsigned i = width; i; --i) { bytes.push_back(static_cast<std::uint8_t>(value >> ((i - 1) * 8))); }
}
class Reader {
    std::span<const std::uint8_t> bytes_;
    std::size_t offset_ = 0;
public:
    explicit Reader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}
    std::uint64_t number(unsigned width) {
        if (width > bytes_.size() - offset_) { throw Error("Truncated packet"); }
        std::uint64_t value = 0;
        while (width--) { value = (value << 8) | bytes_[offset_++]; }
        return value;
    }
    std::string string() {
        const auto size = number(2);
        if (size > bytes_.size() - offset_) { throw Error("Truncated string"); }
        std::string value(reinterpret_cast<const char*>(bytes_.data() + offset_), size);
        offset_ += size;
        return value;
    }
    bool done() const { return offset_ == bytes_.size(); }
};
void string(Bytes& bytes, const std::string& value) {
    number(bytes, value.size(), 2);
    bytes.insert(bytes.end(), value.begin(), value.end());
}
} // namespace
Bytes encode(const Packet& packet) {
    if (packet.name.size() > 32 || packet.text.size() > 2048) { throw Error("Packet string too long"); }
    Bytes bytes{'P', 'N', 1, static_cast<std::uint8_t>(packet.kind)};
    number(bytes, packet.sequence, 8);
    number(bytes, static_cast<std::uint64_t>(packet.time_ms), 8);
    number(bytes, static_cast<std::uint32_t>(packet.sender), 4);
    number(bytes, static_cast<std::uint32_t>(packet.recipient), 4);
    string(bytes, packet.name);
    string(bytes, packet.text);
    return bytes;
}
Packet decode(std::span<const std::uint8_t> bytes) {
    if (bytes.size() > max_packet) { throw Error("Packet too large"); }
    Reader reader(bytes);
    if (reader.number(1) != 'P' || reader.number(1) != 'N' || reader.number(1) != 1) {
        throw Error("Unknown protocol/version");
    }
    const auto kind = reader.number(1);
    if (kind < 1 || kind > static_cast<unsigned>(Kind::Shutdown)) { throw Error("Unknown packet kind"); }
    Packet packet;
    packet.kind = static_cast<Kind>(kind);
    packet.sequence = reader.number(8);
    packet.time_ms = static_cast<std::int64_t>(reader.number(8));
    const auto sender = reader.number(4);
    const auto recipient = reader.number(4);
    if (sender > 0x7fffffff || recipient > 0x7fffffff) { throw Error("Invalid participant PID"); }
    packet.sender = static_cast<int>(sender);
    packet.recipient = static_cast<int>(recipient);
    packet.name = reader.string();
    packet.text = reader.string();
    if (!reader.done() || packet.name.size() > 32 || packet.text.size() > 2048) {
        throw Error("Invalid packet payload");
    }
    return packet;
}
void validate_name(const std::string& name) {
    if (name.empty() || name.size() > 32) { throw Error("Name must contain 1 to 32 UTF-8 bytes"); }
    for (unsigned char c : name) {
        if (std::isspace(c) || c < 32 || c == 127) { throw Error("Name cannot contain whitespace/control characters"); }
    }
}
void validate_text(const std::string& text) {
    if (text.empty() || text.size() > 2048) { throw Error("Message must contain 1 to 2048 UTF-8 bytes"); }
    for (unsigned char c : text) {
        if (c < 32 || c == 127) { throw Error("Message cannot contain control characters"); }
    }
}
std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
void notify(const EventSink& sink, EventKind kind, const std::string& text, const Packet& packet) {
    if (sink) { sink(Event{kind, packet, text}); }
}
} // namespace application
