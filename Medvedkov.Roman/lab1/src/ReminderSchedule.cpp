#include "ReminderSchedule.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace reminder {
void ReminderSchedule::reload(const Configuration& settings, std::time_t now) {
    std::priority_queue<Appointment> replacement;
    std::set<std::string> seen;
    for (const auto& entry : settings.entries) {
        const auto key = entry.key();
        if (!seen.insert(key).second) continue;
        auto due = entry.start;
        const auto old = delivered_.find(key);
        if (!entry.period && old != delivered_.end()) continue;
        auto lower = now - static_cast<std::time_t>(settings.interval);
        if (old != delivered_.end()) lower = std::max(lower, old->second + 1);
        if (due < lower) {
            if (!entry.period) continue;
            const auto distance = lower - due;
            due += ((distance + entry.period - 1) / entry.period) * entry.period;
        }
        replacement.push({due, entry});
    }
    pending_.swap(replacement);
    // Retain delivery history across reloads, including temporarily removed lines.
}

std::vector<ReminderEvent> ReminderSchedule::takeDue(std::time_t now) {
    std::vector<ReminderEvent> result;
    while (!pending_.empty() && pending_.top().due <= now) {
        auto appointment = pending_.top();
        pending_.pop();
        result.push_back(appointment.entry);
        delivered_[appointment.entry.key()] = appointment.due;
        if (appointment.entry.period) {
            const auto period = appointment.entry.period;
            appointment.due += ((now - appointment.due) / period + 1) * period;
            pending_.push(std::move(appointment));
        }
    }
    return result;
}
}
