#pragma once

#include <ctime>
#include <string>
#include <vector>

struct Event {
    std::string key;
    std::time_t start;
    char repeat;
    std::string text;
};

struct Config {
    int interval = 10;
    std::string terminal = "auto";
    std::vector<Event> events;
};

std::time_t parseDate(const std::string& date, const std::string& time);
Config readConfig(const std::string& path);
std::time_t latestOccurrence(const Event& event, std::time_t now);
void addEvent(const std::string& path, const std::vector<std::string>& args);
