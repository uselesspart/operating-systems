#pragma once

#include "Message.hpp"
#include <map>
#include <vector>

namespace chat {
class MessageOrder final {
public:
    void accept(const Message& message);
    std::vector<Message> takeReady();
    bool hasGap() const { return !waiting_.empty(); }
private:
    std::uint64_t expected_ = 1;
    std::int64_t lastTimestamp_ = 0;
    std::map<std::uint64_t, Message> waiting_;
};
}
