#include "Config.h"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <cctype>

namespace {

std::string trim(const std::string& s) {
    std::size_t b = 0;
    std::size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

}

Config parseConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        throw std::runtime_error("Cannot open config file: " + path);
    }

    Config cfg;
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;

        const std::string key = trim(line.substr(0, eq));
        const std::string val = trim(line.substr(eq + 1));

        if (key == "interval") {
            try {
                cfg.intervalSeconds = std::stoi(val);
            } catch (...) {
                throw std::runtime_error("Invalid 'interval' value: " + val);
            }
            if (cfg.intervalSeconds <= 0) {
                throw std::runtime_error("'interval' must be positive");
            }
        } else if (key == "events_file") {
            cfg.eventsFile = val;
        }
    }

    if (cfg.eventsFile.empty()) {
        throw std::runtime_error("'events_file' is not set in config");
    }
    return cfg;
}