#ifndef CHAT_CLOCK_H
#define CHAT_CLOCK_H

#include <cstdint>
#include <string>

namespace chat {

// Milliseconds since the Unix epoch; comparable between processes on one machine
using Millis = std::int64_t;

Millis wallClockMs();
std::string formatTime(Millis timestamp);

}

#endif // CHAT_CLOCK_H
