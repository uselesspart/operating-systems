#pragma once

#include "util/Clock.hpp"

#include <cstdint>
#include <string>

namespace chatlab {

constexpr int kHostId = 0;
constexpr int kEveryone = -1;

struct Member {
    int id = 0;
    std::string name;

    bool operator==(const Member& other) const { return id == other.id && name == other.name; }
};

struct Draft {
    int to = kEveryone;
    Millis sentAt = 0;
    std::string text;
};

struct Post {
    std::uint64_t seq = 0;
    Millis sentAt = 0;
    int from = kHostId;
    int to = kEveryone;
    std::string fromName;
    std::string toName;
    std::string text;

    bool isPrivate() const { return to != kEveryone; }
};

inline bool earlier(const Post& left, const Post& right) {
    if (left.sentAt != right.sentAt) {
        return left.sentAt < right.sentAt;
    }
    return left.seq < right.seq;
}

}
