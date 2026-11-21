#include "util/Clock.hpp"

#include <chrono>
#include <ctime>

namespace chatlab {

Millis wallMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string clockText(Millis stamp) {
    const std::time_t seconds = static_cast<std::time_t>(stamp / 1000);
    std::tm parts{};
    ::localtime_r(&seconds, &parts);
    char text[16] = {};
    std::strftime(text, sizeof(text), "%H:%M:%S", &parts);
    return text;
}

}
