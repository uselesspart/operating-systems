#include "ReminderEvent.hpp"

#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace reminder {
std::string ReminderEvent::key() const {
    return std::to_string(start) + ":" + std::to_string(period) + ":" + message;
}

ReminderEvent ReminderEvent::parse(const std::string& line) {
    static const std::regex pattern(
        R"(^\s*add_event\s+(\d{4}-\d{2}-\d{2})\s+(\d{2}:\d{2}(?::\d{2})?)\s+(?:(-[hdw])\s+)?(\S.*)\s*$)");
    std::smatch match;
    if (!std::regex_match(line, match, pattern)) {
        throw std::runtime_error("Expected: add_event YYYY-MM-DD HH:MM[:SS] [-h|-d|-w] text");
    }
    std::string text = match[4];
    if (text.front() == '-') {
        throw std::runtime_error("Unknown repeat flag or missing text");
    }
    const std::string stamp = match[1].str() + " " + match[2].str();
    std::tm calendar{};
    std::istringstream input(stamp);
    input >> std::get_time(&calendar, stamp.size() == 16 ? "%Y-%m-%d %H:%M" : "%Y-%m-%d %H:%M:%S");
    if (input.fail()) throw std::runtime_error("Invalid date/time");
    const std::tm requested = calendar;
    calendar.tm_isdst = -1;
    const auto start = std::mktime(&calendar);
    if (start == -1 || calendar.tm_year != requested.tm_year ||
        calendar.tm_mon != requested.tm_mon || calendar.tm_mday != requested.tm_mday ||
        calendar.tm_hour != requested.tm_hour || calendar.tm_min != requested.tm_min ||
        calendar.tm_sec != requested.tm_sec) {
        throw std::runtime_error("Date/time does not exist in the local timezone");
    }
    unsigned period = 0;
    if (match[3] == "-h") period = 3600;
    if (match[3] == "-d") period = 86400;
    if (match[3] == "-w") period = 604800;
    return {start, period, text};
}

}
