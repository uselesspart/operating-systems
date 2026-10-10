#include "config.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

#include "common.h"
#include "logger.h"

namespace lab1 {
namespace {

/// Splits a line into whitespace separated fields.
std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> fields;
    std::istringstream stream(line);
    std::string field;
    while (stream >> field) {
        fields.push_back(field);
    }
    return fields;
}

bool isSkippable(const std::string& line) {
    const std::size_t first = line.find_first_not_of(" \t\r\n");
    return first == std::string::npos || line[first] == '#';
}

/// Parses the optional interval field. Returns false unless it is a positive
/// whole number of seconds within the accepted range.
bool parseInterval(const std::string& field, int& interval) {
    char* end = nullptr;
    const long value = std::strtol(field.c_str(), &end, 10);

    if (end == field.c_str() || *end != '\0' || value <= 0 || value > kMaxIntervalSeconds) {
        return false;
    }
    interval = static_cast<int>(value);
    return true;
}

}  // namespace

bool Config::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        log::error("lab1: cannot open config file %s", path.c_str());
        return false;
    }

    std::vector<Rule> parsed;
    std::string line;
    std::size_t lineNumber = 0;

    while (std::getline(file, line)) {
        ++lineNumber;
        if (isSkippable(line)) {
            continue;
        }

        const std::vector<std::string> fields = splitFields(line);
        if (fields.size() != 2 && fields.size() != 3) {
            log::error("lab1: %s:%zu: expected 'folder ignfile [seconds]', got %zu field(s)",
                       path.c_str(), lineNumber, fields.size());
            return false;
        }

        Rule rule;
        rule.folder = fields[0];
        rule.ignoreFile = fields[1];
        if (fields.size() == 3 && !parseInterval(fields[2], rule.intervalSeconds)) {
            log::error("lab1: %s:%zu: '%s' is not an interval in seconds", path.c_str(),
                       lineNumber, fields[2].c_str());
            return false;
        }
        parsed.push_back(rule);
    }

    if (file.bad()) {
        log::error("lab1: error while reading config file %s", path.c_str());
        return false;
    }

    rules_.swap(parsed);
    if (rules_.empty()) {
        log::info("lab1: config file %s contains no rules, the daemon will do nothing",
                  path.c_str());
    } else {
        log::info("lab1: config file %s loaded, %zu rule(s) active", path.c_str(),
                  rules_.size());
    }
    return true;
}

int Config::tickSeconds() const noexcept {
    int tick = kScanIntervalSeconds;
    for (const Rule& rule : rules_) {
        tick = std::min(tick, rule.intervalSeconds);
    }
    return tick;
}

}  // namespace lab1
