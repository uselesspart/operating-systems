#pragma once

#include <string>
#include <vector>
#include <stdexcept>

namespace monitor {

struct ConfigData {
    std::vector<std::string> watchDirs;
};

class ConfigParser {
public:
    static ConfigData load(const std::string& filepath);
};

} // namespace monitor