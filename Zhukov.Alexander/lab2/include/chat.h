#pragma once

#include "ipc.h"
#include "message.h"
#include <functional>
#include <set>

class Chat {
public:
    explicit Chat(int self, std::function<Deadline()> clock = Clock::now);
    static void validate(const Message& message);
    bool accept(const Message& message);
    bool visible(const Message& message) const;
    bool idle() const;
    const History& history() const { return messages; }

private:
    int self;
    std::function<Deadline()> clock;
    Deadline last_activity;
    History messages;
    std::set<std::pair<int, std::uint64_t>> seen;
};
