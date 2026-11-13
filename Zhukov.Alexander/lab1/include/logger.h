#pragma once

#include <string>

class Logger {
public:
    Logger();
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    static void info(const std::string& message);
    static void error(const std::string& message);
    static void event(const std::string& message);
};
