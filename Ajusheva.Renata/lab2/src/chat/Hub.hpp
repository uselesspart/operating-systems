#pragma once

#include "chat/Limits.hpp"
#include "chat/Post.hpp"

#include <chrono>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

#include <sys/types.h>

namespace chatlab {

struct Accepted {
    bool ok = false;
    std::string error;
    Post post;
};

struct Released {
    std::vector<Post> forHost;
    std::vector<Post> relayed;
};

struct Batch {
    std::vector<Post> posts;
    std::vector<std::string> notes;
};

struct Silent {
    int id = 0;
    pid_t pid = 0;
    std::string name;
};

class Hub {
  public:
    static constexpr const char* kHostName = "Хост";

    explicit Hub(std::chrono::milliseconds settle = kSettleDelay);

    bool hasRoom() const;
    std::string join(int id, pid_t pid, const std::string& wanted, Millis now);
    void leave(int id, const std::string& reason);
    void notify(int id, const std::string& note);

    bool isMember(int id) const;
    std::optional<std::string> nameOf(int id) const;
    std::vector<Member> roster() const;
    std::uint64_t rosterStamp() const;

    Accepted submit(int from, const Draft& draft, Millis now);
    Released release(Millis now);
    Batch take(int id);
    std::vector<Silent> silentFor(Millis now, std::chrono::milliseconds limit) const;

  private:
    struct Seat {
        pid_t pid = 0;
        std::string name;
        Millis lastSpoke = 0;
        std::deque<Post> inbox;
        std::deque<std::string> notes;
    };

    bool nameBusy(const std::string& name) const;
    std::optional<std::string> nameLocked(int id) const;
    void noteOthers(int except, const std::string& note);
    void deliver(int id, const Post& post);

    const std::chrono::milliseconds settle_;
    mutable std::mutex mutex_;
    std::map<int, Seat> seats_;
    std::vector<Post> waiting_;
    Millis watermark_ = 0;
    std::uint64_t nextSeq_ = 1;
    std::uint64_t rosterStamp_ = 1;
};

}
