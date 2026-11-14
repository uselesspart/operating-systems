#include "Config.h"

#include <sys/file.h>
#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::string readFileLocked(const std::filesystem::path& path) {
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd == -1) {
        throw std::runtime_error("Cannot open config " + path.string() + ": " +
                                 std::strerror(errno));
    }

    if (::flock(fd, LOCK_SH) != 0) {
        const int savedErrno = errno;
        ::close(fd);
        throw std::runtime_error("Cannot lock config: " +
                                 std::string(std::strerror(savedErrno)));
    }

    std::string content;
    char buffer[4096];
    while (true) {
        const ssize_t length = ::read(fd, buffer, sizeof(buffer));
        if (length == 0) {
            break;
        }
        if (length < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int savedErrno = errno;
            ::close(fd);
            throw std::runtime_error("Cannot read config: " +
                                     std::string(std::strerror(savedErrno)));
        }
        content.append(buffer, static_cast<std::size_t>(length));
        if (content.size() > 1024U * 1024U) {
            ::close(fd);
            throw std::runtime_error("Config exceeds 1 MiB");
        }
    }
    ::close(fd); // Заодно отпускаем общую блокировку.
    return content;
}

std::time_t parseDateTime(const std::string& date, const std::string& time) {
    std::tm parsed = {};
    std::istringstream input(date + " " + time);
    input >> std::get_time(&parsed, "%Y-%m-%d %H:%M");
    if (input.fail() || input.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("Invalid date/time: " + date + " " + time);
    }

    parsed.tm_isdst = -1;
    const std::time_t timestamp = std::mktime(&parsed);
    if (timestamp == static_cast<std::time_t>(-1)) {
        throw std::runtime_error("Date/time is outside the supported range");
    }

    std::tm local = {};
    if (::localtime_r(&timestamp, &local) == nullptr) {
        throw std::runtime_error("Cannot interpret local date/time");
    }
    char normalized[17];
    if (std::strftime(normalized, sizeof(normalized), "%Y-%m-%d %H:%M", &local) == 0 ||
        date + " " + time != normalized) {
        throw std::runtime_error("Nonexistent or invalid local date/time: " +
                                 date + " " + time);
    }
    return timestamp;
}

std::string stableId(const std::string& line) {
    // FNV-1a не меняется между запусками, в отличие от std::hash.
    std::uint64_t value = UINT64_C(14695981039346656037);
    for (const unsigned char byte : line) {
        value ^= byte;
        value *= UINT64_C(1099511628211);
    }
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << value;
    return out.str();
}

ReminderEvent parseEvent(const std::string& line) {
    std::istringstream input(line);
    std::string keyword;
    std::string date;
    std::string time;
    std::string firstWord;
    if (!(input >> keyword >> date >> time >> firstWord) || keyword != "event") {
        throw std::runtime_error("Expected: event YYYY-MM-DD HH:MM [-h|-d|-w] text");
    }

    ReminderEvent event;
    event.firstDue = parseDateTime(date, time);

    if (firstWord == "-h" || firstWord == "-d" || firstWord == "-w") {
        event.repeat = firstWord == "-h" ? Repeat::Hourly
                     : firstWord == "-d" ? Repeat::Daily : Repeat::Weekly;
        std::getline(input >> std::ws, event.text);
    } else if (firstWord == "--") {
        std::getline(input >> std::ws, event.text);
    } else {
        std::string rest;
        std::getline(input, rest);
        event.text = firstWord + rest;
    }
    event.text = trim(event.text);
    if (event.text.empty() || event.text.size() > 4096) {
        throw std::runtime_error("Event text must contain 1-4096 characters");
    }
    event.id = stableId(line);
    return event;
}

int parseInterval(const std::string& value) {
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos) {
        throw std::runtime_error("interval must be a positive number of seconds");
    }
    unsigned long long parsed = 0;
    try {
        parsed = std::stoull(value);
    } catch (const std::exception&) {
        throw std::runtime_error("interval is not a valid number");
    }
    if (parsed == 0 || parsed > 86400) {
        throw std::runtime_error("interval must be between 1 and 86400 seconds");
    }
    return static_cast<int>(parsed);
}

} // конец анонимного пространства имён

Configuration loadConfiguration(const std::filesystem::path& absoluteConfigPath) {
    Configuration config;
    config.stateDirectory = absoluteConfigPath.parent_path() / "state";
    const std::string content = readFileLocked(absoluteConfigPath);
    std::istringstream input(content);
    std::string line;
    unsigned long lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }
        try {
            std::istringstream firstToken(line);
            std::string key;
            firstToken >> key;
            if (key == "event") {
                config.events.push_back(parseEvent(line));
                continue;
            }
            const auto equalPos = line.find('=');
            if (equalPos == std::string::npos) {
                throw std::runtime_error("Expected key=value or event line");
            }
            key = trim(line.substr(0, equalPos));
            const std::string value = trim(line.substr(equalPos + 1));
            if (key == "interval") {
                config.intervalSeconds = parseInterval(value);
            } else if (key == "state_dir") {
                if (value.empty()) {
                    throw std::runtime_error("state_dir cannot be empty");
                }
                std::filesystem::path statePath(value);
                if (statePath.is_relative()) {
                    statePath = absoluteConfigPath.parent_path() / statePath;
                }
                config.stateDirectory = statePath.lexically_normal();
            } else if (key == "terminal") {
                if (value != "auto" && value != "xterm" &&
                    value != "gnome-terminal" && value != "konsole" &&
                    value != "x-terminal-emulator") {
                    throw std::runtime_error("Unsupported terminal: " + value);
                }
                config.terminal = value;
            } else {
                throw std::runtime_error("Unknown config key: " + key);
            }
        } catch (const std::exception& error) {
            throw std::runtime_error("Config line " + std::to_string(lineNumber) +
                                     ": " + error.what());
        }
    }
    return config;
}
