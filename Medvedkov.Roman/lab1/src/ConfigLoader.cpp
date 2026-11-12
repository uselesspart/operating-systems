#include "ConfigLoader.hpp"

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace reminder {
Configuration ConfigLoader::load(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Cannot read config: " + path);
    Configuration settings;
    std::string line;
    unsigned number = 0;
    while (std::getline(file, line)) {
        ++number;
        const auto first = line.find_first_not_of(" \t\r");
        if (first == std::string::npos || line[first] == '#') continue;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        try {
            std::istringstream fields(line);
            std::string directive, value, extra;
            fields >> directive;
            if (directive == "interval" || directive == "terminal") {
                if (!(fields >> value) || (fields >> extra)) throw std::runtime_error("Expected one value");
                if (directive == "terminal") {
                    settings.terminal = value;
                } else {
                    if (!std::regex_match(value, std::regex("[0-9]{1,5}"))) throw std::runtime_error("Invalid interval");
                    settings.interval = std::stoul(value);
                    if (settings.interval == 0 || settings.interval > 86400) throw std::runtime_error("Interval must be 1..86400");
                }
            } else {
                settings.entries.push_back(ReminderEvent::parse(line));
            }
        } catch (const std::exception& error) {
            throw std::runtime_error("Line " + std::to_string(number) + ": " + error.what());
        }
    }
    if (file.bad()) throw std::runtime_error("Config read failed");
    if (settings.terminal.find('/') != std::string::npos && settings.terminal.front() != '/') {
        throw std::runtime_error("Terminal path must be absolute, or a program name from PATH");
    }
    return settings;
}

}
