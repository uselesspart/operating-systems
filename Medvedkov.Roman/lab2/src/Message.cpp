#include "Message.hpp"

#include <algorithm>
#include <stdexcept>
#include <type_traits>

namespace chat {
static_assert(std::is_trivially_copyable<Message>::value, "IPC packet must be plain data");

Message Message::compose(Party from, Party to, std::uint64_t serial,
                         std::int64_t time, const std::string& body) {
    Message result;
    if (body.empty() || body.size() > MaxText || body.find('\0') != std::string::npos) {
        throw std::invalid_argument("Message must contain 1..768 UTF-8 bytes, without NUL");
    }
    result.sender = from;
    result.recipient = to;
    result.sequence = serial;
    result.sentAtNs = time;
    result.length = static_cast<std::uint32_t>(body.size());
    std::copy(body.begin(), body.end(), result.text.begin());
    result.validate();
    return result;
}

void Message::validate() const {
    if (magic != 0x524d4348 || reserved != 0 ||
        (kind != PacketKind::Request && kind != PacketKind::Delivery) ||
        (sender != Party::Host && sender != Party::Client) ||
        (recipient != Party::Everyone && recipient != Party::Host && recipient != Party::Client) ||
        sequence == 0 || sentAtNs <= 0 || length == 0 || length > MaxText) {
        throw std::runtime_error("Invalid chat packet");
    }
    if (std::find(text.begin(), text.begin() + length, '\0') != text.begin() + length) {
        throw std::runtime_error("NUL in chat packet");
    }
}

bool Message::visibleTo(Party viewer) const {
    return viewer != Party::Everyone &&
           (recipient == Party::Everyone || sender == viewer || recipient == viewer);
}

std::string Message::body() const {
    validate();
    return {text.data(), length};
}
}
