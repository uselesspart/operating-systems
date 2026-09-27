#include "config.hpp"
#include <fstream>
#include <algorithm>

namespace monitor {

ConfigData ConfigParser::load(const std::string& filepath) {
    ConfigData config;
    std::ifstream file(filepath);

    if (!file.is_open()) {
        throw std::runtime_error("Failed to open config file: " + filepath);
    }

    std::string line;
    while (std::getline(file, line)) {
        line.erase(0, line.find_first_not_of(" \t\r\n"));
        line.erase(line.find_last_not_of(" \t\r\n") + 1);

        if (line.empty() || line[0] == '#') continue;

        auto delimPos = line.find('=');
        if (delimPos == std::string::npos) continue;

        std::string key = line.substr(0, delimPos);
        std::string value = line.substr(delimPos + 1);

        key.erase(key.find_last_not_of(" \t") + 1);
        value.erase(0, value.find_first_not_of(" \t"));

        if (key == "DIR" && !value.empty()) {
            config.watchDirs.push_back(value);
        }
    }

    if (config.watchDirs.empty()) {
        throw std::runtime_error("No directories (DIR) specified in config");
    }

    return config;
}

} // namespace monitor