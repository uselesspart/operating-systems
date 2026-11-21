#pragma once

#include "conn/conn.hpp"
#include "link/GlobalSemaphore.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace chatlab {

using Bytes = std::vector<std::uint8_t>;

enum class LinkStatus { Ok, TimedOut, Broken, Malformed };

const char* statusText(LinkStatus status);

class Link {
  public:
    enum class Side { Host, Guest };

    static constexpr std::size_t kFrameLimit = 56 * 1024;

    Link(const ConnKey& key, Side side);

    Link(const Link&) = delete;
    Link& operator=(const Link&) = delete;

    LinkStatus send(const Bytes& payload);
    LinkStatus receive(Bytes& payload, std::chrono::milliseconds timeout);

  private:
    GlobalSemaphore up_;
    GlobalSemaphore down_;
    Conn conn_;
    GlobalSemaphore& inbound_;
    GlobalSemaphore& outbound_;
};

}
