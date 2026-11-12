#include "Event.hpp"

#include <sstream>

const char* repeatName(Repeat repeat) {
    switch (repeat) {
    case Repeat::None:
        return "none";
    case Repeat::Hour:
        return "hour";
    case Repeat::Day:
        return "day";
    case Repeat::Week:
        return "week";
    }
    return "none";
}

long long repeatPeriod(Repeat repeat) {
    switch (repeat) {
    case Repeat::None:
        return 0;
    case Repeat::Hour:
        return 3600;
    case Repeat::Day:
        return 86400;
    case Repeat::Week:
        return 604800;
    }
    return 0;
}

static bool parseRepeat(const std::string& name, Repeat& repeat) {
    if (name == "none") {
        repeat = Repeat::None;
        return true;
    }
    if (name == "hour") {
        repeat = Repeat::Hour;
        return true;
    }
    if (name == "day") {
        repeat = Repeat::Day;
        return true;
    }
    if (name == "week") {
        repeat = Repeat::Week;
        return true;
    }
    return false;
}

bool parseEvent(const std::string& line, Event& event) {
    std::istringstream input(line);
    long long timestamp = 0;
    std::string repeatText;
    if (!(input >> timestamp >> repeatText)) {
        return false;
    }
    if (timestamp <= 0) {
        return false;
    }

    Repeat repeat;
    if (!parseRepeat(repeatText, repeat)) {
        return false;
    }

    std::string rest;
    std::getline(input, rest);
    std::size_t start = rest.find_first_not_of(" \t");
    if (start == std::string::npos) {
        return false;
    }

    event.timestamp = timestamp;
    event.repeat = repeat;
    event.text = rest.substr(start);
    return true;
}

std::string formatEvent(const Event& event) {
    return std::to_string(event.timestamp) + " " + repeatName(event.repeat) + " " + event.text;
}

void moveToFuture(Event& event, long long now) {
    long long period = repeatPeriod(event.repeat);
    if (period == 0) {
        return;
    }
    while (event.timestamp <= now) {
        event.timestamp += period;
    }
}
