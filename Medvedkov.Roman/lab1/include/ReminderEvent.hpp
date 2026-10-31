#pragma once

#include <ctime>
#include <string>

namespace reminder {
// One reminder and its repetition period (zero means no repetition).
struct ReminderEvent {
    std::time_t start;
    unsigned period;
    std::string message;

    std::string key() const;
    static ReminderEvent parse(const std::string& line);
};
}
