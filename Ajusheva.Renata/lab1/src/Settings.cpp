#include "Settings.hpp"

#include "FileDescriptor.hpp"
#include "PathUtil.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <set>
#include <sstream>
#include <utility>

#include <fcntl.h>
#include <unistd.h>

namespace diskmond {

namespace {

constexpr const char* kBlanks = " \t\r\n";

constexpr const char* kUtf8Bom = "\xEF\xBB\xBF";

struct Candidate {
    std::string directory;
    std::string place;
};

std::string stripBlanks(const std::string& text) {
    const auto first = text.find_first_not_of(kBlanks);
    if (first == std::string::npos) {
        return {};
    }
    return text.substr(first, text.find_last_not_of(kBlanks) - first + 1);
}

std::string lowercase(std::string text) {
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char symbol) { return static_cast<char>(std::tolower(symbol)); });
    return text;
}


std::pair<std::string, std::string> splitDirective(const std::string& line) {
    const auto keywordEnd = line.find_first_of(" \t=");
    if (keywordEnd == std::string::npos) {
        return {lowercase(line), {}};
    }
    std::string value = stripBlanks(line.substr(keywordEnd));
    if (!value.empty() && value.front() == '=') {
        value = stripBlanks(value.substr(1));
    }
    return {lowercase(line.substr(0, keywordEnd)), value};
}


std::vector<std::string> keepOutermost(const std::vector<Candidate>& candidates,
                                       std::vector<std::string>& remarks) {
    std::set<std::string> listed;
    for (const Candidate& candidate : candidates) {
        listed.insert(candidate.directory);
    }

    std::set<std::string> kept;
    for (const Candidate& candidate : candidates) {
        std::string ancestor = candidate.directory;
        bool covered = false;
        while (ancestor != "/" && !covered) {
            ancestor = path::parentOf(ancestor);
            covered = listed.count(ancestor) > 0;
        }
        if (covered) {
            remarks.push_back(candidate.place + ": '" + candidate.directory + "' is inside '" + ancestor +
                              "', which is watched with all its subdirectories (line ignored)");
        } else if (!kept.insert(candidate.directory).second) {
            remarks.push_back(candidate.place + ": '" + candidate.directory +
                              "' is already listed (line ignored)");
        }
    }
    return {kept.begin(), kept.end()};
}

}  

ParsedSettings parseSettings(std::istream& input, const std::string& baseDir, const std::string& origin) {
    ParsedSettings parsed;
    std::vector<Candidate> candidates;

    std::string line;
    for (int number = 1; std::getline(input, line); ++number) {
        if (number == 1 && line.compare(0, 3, kUtf8Bom) == 0) {
            line.erase(0, 3);
        }
        const std::string text = stripBlanks(line);
        if (text.empty() || text.front() == '#') {
            continue;
        }

        const std::string place = origin + ":" + std::to_string(number);
        const auto [keyword, value] = splitDirective(text);
        if (keyword != "watch" && keyword != "ignore" && keyword != "logfile") {
            parsed.remarks.push_back(place + ": unknown keyword '" + keyword + "' (line ignored)");
            continue;
        }
        if (value.empty()) {
            parsed.remarks.push_back(place + ": '" + keyword + "' without a path (line ignored)");
            continue;
        }

        const std::string fullPath = value.front() == '/' ? value : path::join(baseDir, value);
        if (keyword == "ignore") {
            parsed.settings.ignored.push_back(path::resolve(fullPath));
            continue;
        }
        if (keyword == "logfile") {
            parsed.settings.logFile = path::resolve(fullPath);
            continue;
        }
        const auto directory = path::canonical(fullPath);
        if (!directory) {
            const int code = errno;
            parsed.remarks.push_back(place + ": cannot use '" + value + "': " + std::strerror(code) +
                                     " (line ignored)");
        } else if (!path::isDirectory(*directory)) {
            parsed.remarks.push_back(place + ": '" + value + "' is not a directory (line ignored)");
        } else {
            candidates.push_back({*directory, place});
        }
    }

    parsed.settings.directories = keepOutermost(candidates, parsed.remarks);

    auto& ignored = parsed.settings.ignored;
    std::sort(ignored.begin(), ignored.end());
    ignored.erase(std::unique(ignored.begin(), ignored.end()), ignored.end());
    return parsed;
}

std::optional<ParsedSettings> readSettingsFile(const std::string& file, std::string& error) {
    const FileDescriptor fd(::open(file.c_str(), O_RDONLY | O_CLOEXEC));
    if (!fd.isOpen()) {
        const int code = errno;
        error = "config " + file + " cannot be opened: " + std::strerror(code);
        return std::nullopt;
    }

    std::string content;
    char chunk[4096];
    for (;;) {
        const ssize_t length = ::read(fd.get(), chunk, sizeof chunk);
        if (length > 0) {
            content.append(chunk, static_cast<std::size_t>(length));
        } else if (length == 0) {
            break;
        } else if (errno != EINTR) {
            const int code = errno;
            error = "config " + file + " cannot be read: " + std::strerror(code);
            return std::nullopt;
        }
    }

    std::istringstream input(content);
    return parseSettings(input, path::parentOf(file), file);
}

}  
