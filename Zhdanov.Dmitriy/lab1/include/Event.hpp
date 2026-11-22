#pragma once

#include <string>

enum class Repeat {
    None,
    Hour,
    Day,
    Week
};

struct Event {
    long long timestamp;
    Repeat repeat;
    std::string text;
};

bool parseEvent(const std::string& line, Event& event);
std::string formatEvent(const Event& event);
const char* repeatName(Repeat repeat);
long long repeatPeriod(Repeat repeat);
void moveToFuture(Event& event, long long now);
