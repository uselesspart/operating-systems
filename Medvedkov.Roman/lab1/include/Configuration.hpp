#pragma once

#include "ReminderEvent.hpp"
#include <string>
#include <vector>

namespace reminder {
struct Configuration {
    unsigned interval = 10;
    std::string terminal = "xterm";
    std::vector<ReminderEvent> entries;
};
}
