#include "config.h"

#include <sys/stat.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>
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

/// Directory part of an absolute path, "/" for a file lying in the root.
std::string directoryOf(const std::string& filePath) {
    const std::size_t slash = filePath.find_last_of('/');
    if (slash == std::string::npos || slash == 0) {
        return std::string("/");
    }
    return filePath.substr(0, slash);
}

/// Resolves the folder field of a rule to an absolute path, relative to the
/// directory of the configuration file. The daemon changes its working
/// directory to "/" right after the fork, so a relative folder has to be
/// anchored to the config file, not to the working directory: "tmp/x" means
/// "<config directory>/tmp/x" and never "/tmp/x". realpath() also collapses
/// "..", so a folder escaping to the root is caught below just like "/".
/// Returns false for a missing folder, for anything but a directory, and for
/// the root directory itself, whose content the daemon must never touch.
bool resolveFolder(const std::string& configDirectory, const std::string& field,
                   std::string& folder) {
    const std::string joined =
        (!field.empty() && field.front() == '/') ? field : joinPath(configDirectory, field);

    char* resolved = realpath(joined.c_str(), nullptr);
    if (resolved == nullptr) {
        log::error("lab1: cannot resolve folder %s: %s", joined.c_str(), std::strerror(errno));
        return false;
    }
    folder.assign(resolved);
    std::free(resolved);

    struct stat info {};
    if (stat(folder.c_str(), &info) == -1 || !S_ISDIR(info.st_mode)) {
        log::error("lab1: %s is not a directory", folder.c_str());
        return false;
    }
    if (folder == "/") {
        log::error("lab1: refusing to manage the root directory '/'");
        return false;
    }
    return true;
}

}  // namespace

bool Config::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        log::error("lab1: cannot open config file %s", path.c_str());
        return false;
    }

    // The daemon stores the config path as resolved by realpath() at startup,
    // so it is absolute and its directory is meaningful even after chdir("/").
    const std::string configDirectory = directoryOf(path);

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
        if (!resolveFolder(configDirectory, fields[0], rule.folder)) {
            log::error("lab1: %s:%zu: bad folder '%s'", path.c_str(), lineNumber,
                       fields[0].c_str());
            return false;
        }
        if (fields[1].find('/') != std::string::npos) {
            log::error("lab1: %s:%zu: guard file '%s' must be a plain file name", path.c_str(),
                       lineNumber, fields[1].c_str());
            return false;
        }
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

}  // namespace lab1
