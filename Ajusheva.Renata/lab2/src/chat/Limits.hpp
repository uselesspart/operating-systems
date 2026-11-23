#pragma once

#include <chrono>
#include <cstddef>

namespace chatlab {

using namespace std::chrono_literals;

constexpr std::size_t kTextLimit = 1000;
constexpr std::size_t kNameLimit = 24;
constexpr std::size_t kPostsPerReply = 20;
constexpr std::size_t kDraftsPerRequest = 20;
constexpr std::size_t kMemberLimit = 100;
constexpr std::size_t kNotesPerReply = 32;
constexpr std::size_t kNoteLimit = 256;

constexpr std::chrono::milliseconds kPollPeriod = 200ms;
constexpr std::chrono::milliseconds kSettleDelay = 450ms;
constexpr std::chrono::seconds kSilenceLimit = 60s;

}
