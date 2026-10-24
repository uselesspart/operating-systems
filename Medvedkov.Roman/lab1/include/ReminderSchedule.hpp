#pragma once

#include "Configuration.hpp"
#include <map>
#include <queue>
#include <vector>

namespace reminder {
// Maintains due times and delivery history across configuration reloads.
class ReminderSchedule {
public:
    void reload(const Configuration& settings, std::time_t now);
    std::vector<ReminderEvent> takeDue(std::time_t now);
private:
    struct Appointment {
        std::time_t due;
        ReminderEvent entry;
        bool operator<(const Appointment& other) const { return due > other.due; }
    };
    std::priority_queue<Appointment> pending_;
    std::map<std::string, std::time_t> delivered_;
};
}
