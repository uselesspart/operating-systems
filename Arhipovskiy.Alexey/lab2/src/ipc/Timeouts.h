#ifndef CHAT_TIMEOUTS_H
#define CHAT_TIMEOUTS_H

#include <chrono>

namespace chat {

using namespace std::chrono_literals;

// Every wait on a semaphore, a signal or a peer connection gives up after this time
constexpr std::chrono::milliseconds kIpcTimeout = 5s;

}

#endif // CHAT_TIMEOUTS_H
