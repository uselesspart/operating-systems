#ifndef CHAT_LIMITS_H
#define CHAT_LIMITS_H

#include <chrono>
#include <cstddef>

namespace chat {

using namespace std::chrono_literals;

// Chosen so that one frame (kMaxBatch messages plus the participant list) stays well below
// Channel::kMaxFrameSize
constexpr std::size_t kMaxTextBytes = 2000;
constexpr std::size_t kMaxNameBytes = 32;
constexpr std::size_t kMaxBatch = 16;
constexpr std::size_t kMaxParticipants = 200;

constexpr std::chrono::milliseconds kPollInterval = 200ms;
constexpr std::chrono::milliseconds kOrderingDelay = 400ms;
constexpr std::chrono::seconds kDefaultIdleLimit = 60s;

}

#endif // CHAT_LIMITS_H
