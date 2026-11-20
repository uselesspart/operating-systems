#include "MessageOrder.hpp"

#include <stdexcept>

namespace chat {
void MessageOrder::accept(const Message& message) {
    message.validate();
    if (message.kind != PacketKind::Request || message.sequence < expected_ ||
        waiting_.size() >= 64 || waiting_.count(message.sequence)) {
        throw std::runtime_error("Duplicate, stale or excessive message sequence");
    }
    waiting_.emplace(message.sequence, message);
}

std::vector<Message> MessageOrder::takeReady() {
    std::vector<Message> result;
    for (;;) {
        auto next = waiting_.find(expected_);
        if (next == waiting_.end()) break;
        auto message = next->second;
        if (message.sentAtNs <= lastTimestamp_) throw std::runtime_error("Message timestamp order violated");
        lastTimestamp_ = message.sentAtNs;
        message.kind = PacketKind::Delivery;
        result.push_back(message);
        waiting_.erase(next);
        ++expected_;
    }
    return result;
}
}
