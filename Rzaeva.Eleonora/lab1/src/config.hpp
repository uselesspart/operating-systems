#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace dirclean {

// Строка конфига вида "folder ignfile".
struct Rule {
    std::filesystem::path folder;
    std::filesystem::path ignFile;
};

struct Config {
    int intervalSeconds = 20;
    std::vector<Rule> rules;
};

struct ConfigParseResult {
    bool ok = false;
    std::string error;
    std::vector<std::string> warnings;
    Config config;
};

ConfigParseResult parseConfigFile(const std::filesystem::path& path);

}
