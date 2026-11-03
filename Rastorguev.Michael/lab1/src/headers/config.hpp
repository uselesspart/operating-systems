#pragma once

#include <ctime>
#include <filesystem>
#include <string>
#include <vector>

struct Event {
    time_t when;
    char repeat;
    std::string text;
};

time_t advance_time(time_t t, char repeat);

struct Config {
    unsigned interval = 10;
    std::vector<Event> events;

    static bool load(const std::filesystem::path& path, Config& out, std::string& err);
};
