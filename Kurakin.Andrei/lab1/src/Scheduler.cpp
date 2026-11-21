#include "Scheduler.h"

#include <ctime>

std::optional<std::time_t> currentOccurrence(const ReminderEvent& event,
                                              std::time_t now) {
    if (now < event.firstDue) {
        return std::nullopt;
    }
    if (event.repeat == Repeat::Once) {
        return event.firstDue;
    }
    if (event.repeat == Repeat::Hourly) {
        const std::time_t periods = (now - event.firstDue) / 3600;
        return event.firstDue + periods * 3600;
    }

    // Для дневных и недельных событий учитываем местное время и переходы DST.
    std::tm startTime = {};
    std::tm currentTime = {};
    if (::localtime_r(&event.firstDue, &startTime) == nullptr ||
        ::localtime_r(&now, &currentTime) == nullptr) {
        return std::nullopt;
    }
    std::tm candidate = currentTime;
    candidate.tm_hour = startTime.tm_hour;
    candidate.tm_min = startTime.tm_min;
    candidate.tm_sec = startTime.tm_sec;
    candidate.tm_isdst = -1;

    const int calendarDays = event.repeat == Repeat::Daily ? 1 : 7;
    if (event.repeat == Repeat::Weekly) {
        candidate.tm_mday -= (currentTime.tm_wday - startTime.tm_wday + 7) % 7;
    }
    std::time_t due = std::mktime(&candidate);
    if (due > now) {
        candidate.tm_mday -= calendarDays;
        candidate.tm_isdst = -1;
        due = std::mktime(&candidate);
    }
    if (due == static_cast<std::time_t>(-1) || due < event.firstDue) {
        return std::nullopt;
    }
    return due;
}
