#include "chat.h"

#include <QString>
#include <algorithm>
#include <stdexcept>
#include <tuple>

Chat::Chat(int id, std::function<Deadline()> now)
    : self(id), clock(std::move(now)), last_activity(clock()) {}

void Chat::validate(const Message& message) {
    encode(message);
    if (message.kind != MessageKind::Public && message.kind != MessageKind::Private) {
        throw std::runtime_error("Invalid message type");
    }
    if (QString::fromUtf8(message.text.data(), message.text.size()).trimmed().isEmpty()) {
        throw std::runtime_error("Empty message");
    }
    if (message.number == 0) {
        throw std::runtime_error("Invalid message number");
    }
}

bool Chat::visible(const Message& message) const {
    return message.kind == MessageKind::Public ||
           (message.kind == MessageKind::Private &&
            (message.sender == self || message.recipient == self));
}

bool Chat::accept(const Message& message) {
    if (message.kind == MessageKind::System) {
        return false;
    }
    validate(message);
    if (!seen.emplace(message.sender, message.number).second) {
        return false;
    }
    if (self == 0 && message.sender == 1) {
        last_activity = clock();
    }
    if (visible(message)) {
        auto before = [](const Message& left, const Message& right) {
            return std::tie(left.sent_at, left.sender, left.number) <
                   std::tie(right.sent_at, right.sender, right.number);
        };
        messages.insert(std::lower_bound(messages.begin(), messages.end(), message, before), message);
    }
    return true;
}

bool Chat::idle() const {
    return self == 0 && clock() - last_activity > std::chrono::seconds(60);
}
