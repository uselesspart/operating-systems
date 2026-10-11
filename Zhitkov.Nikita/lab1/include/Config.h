#pragma once

#include <string>

struct Config {
    int intervalSeconds = 10;
    std::string eventsFile;
};

// Бросает std::runtime_error при ошибке
Config parseConfig(const std::string& path);