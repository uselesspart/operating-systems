#include "util/Clock.h"

#include <chrono>
#include <ctime>

namespace chat {

Millis wallClockMs()
{
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string formatTime(Millis timestamp)
{
    const std::time_t seconds = static_cast<std::time_t>(timestamp / 1000);
    std::tm local{};
    localtime_r(&seconds, &local);
    char buffer[16];
    std::strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
    return buffer;
}

}
