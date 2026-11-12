#pragma once

#include <iosfwd>
#include <optional>
#include <string>
#include <vector>

namespace diskmond {


constexpr const char* kDefaultLogFile = "/var/log/diskmond.log";


struct Settings {
    std::vector<std::string> directories;
    std::vector<std::string> ignored;
    std::string logFile = kDefaultLogFile;
};

struct ParsedSettings {
    Settings settings;
    std::vector<std::string> remarks;  
};


ParsedSettings parseSettings(std::istream& input, const std::string& baseDir, const std::string& origin);


std::optional<ParsedSettings> readSettingsFile(const std::string& file, std::string& error);

}  
