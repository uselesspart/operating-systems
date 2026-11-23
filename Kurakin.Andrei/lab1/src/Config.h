#pragma once

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

enum class Repeat { Once, Hourly, Daily, Weekly };

struct ReminderEvent {
    std::time_t firstDue = 0;
    Repeat repeat = Repeat::Once;
    std::string text;
    std::string id;
};

struct Configuration {
    int intervalSeconds = 10;
    std::filesystem::path stateDirectory;
    std::string terminal = "auto";
    std::vector<ReminderEvent> events;
};

Configuration loadConfiguration(const std::filesystem::path& absoluteConfigPath);
