#pragma once

#include "chat/Packets.hpp"
#include "link/Link.hpp"

#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include <sys/types.h>

namespace chatlab {

class GuestError : public std::runtime_error {
  public:
    using std::runtime_error::runtime_error;
};

struct Exchange {
    bool ok = false;
    std::string failure;
    packets::Reply reply;
};

class GuestSession {
  public:
    GuestSession(pid_t host, std::string wantedName);

    packets::Reply connect();
    Exchange poll(std::vector<Draft> drafts);
    Exchange leave(std::vector<Draft> drafts);

    int id() const noexcept { return id_; }
    pid_t host() const noexcept { return host_; }

  private:
    Exchange roundTrip(const packets::Request& request);

    const pid_t host_;
    const std::string wantedName_;
    std::unique_ptr<Link> link_;
    int id_ = 0;
};

}
