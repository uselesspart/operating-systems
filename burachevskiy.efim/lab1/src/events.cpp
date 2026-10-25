#include "events.h"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iomanip>
#include <map>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <sys/file.h>
#include <unistd.h>

namespace {
class ConfigLock {
    int fd;
public:
    ConfigLock(const std::string& path, int mode) {
        fd = open((path + ".lock").c_str(), O_CREAT | O_RDWR | O_CLOEXEC | O_NOFOLLOW, 0600);
        if (fd == -1) throw std::runtime_error("open config lock: " + std::string(strerror(errno)));
        if (flock(fd, mode) == -1) {
            const std::string message = strerror(errno);
            close(fd);
            throw std::runtime_error("flock config: " + message);
        }
    }
    ~ConfigLock() { close(fd); }
    ConfigLock(const ConfigLock&) = delete;
    ConfigLock& operator=(const ConfigLock&) = delete;
};

char parseRepeat(const std::string& flag) {
    if (flag == "-") return '-';
    if (flag == "-h" || flag == "-d" || flag == "-w") return flag[1];
    throw std::runtime_error("Повтор: только -, -h, -d или -w");
}

std::string trim(const std::string& value) {
    const auto first = value.find_first_not_of(" \t\r");
    if (first == std::string::npos) return "";
    return value.substr(first, value.find_last_not_of(" \t\r") - first + 1);
}

Config load(const std::string& path) {
    std::ifstream file(path);
    if (!file) throw std::runtime_error("Не открыть конфигурацию: " + path);
    Config config;
    std::map<std::string, int> duplicates;
    bool intervalSeen = false;
    bool terminalSeen = false;
    std::string line;
    int number = 0;
    while (std::getline(file, line)) {
        ++number;
        line = trim(line);
        if (line.empty() || line.front() == '#') continue;
        try {
            if (line.rfind("interval=", 0) == 0) {
                const std::string value = trim(line.substr(9));
                if (intervalSeen || !std::regex_match(value, std::regex("[0-9]+")))
                    throw std::runtime_error("Некорректный или повторный interval");
                config.interval = std::stoi(value);
                if (config.interval < 1 || config.interval > 3600)
                    throw std::runtime_error("interval должен быть от 1 до 3600 секунд");
                intervalSeen = true;
            } else if (line.rfind("terminal=", 0) == 0) {
                if (terminalSeen) throw std::runtime_error("Повторный terminal");
                config.terminal = trim(line.substr(9));
                if (config.terminal.empty()) throw std::runtime_error("Пустой terminal");
                terminalSeen = true;
            } else {
                std::istringstream input(line);
                std::string date, time, flag, text;
                if (!(input >> date >> time >> flag)) throw std::runtime_error("Нужны дата, время, повтор и текст");
                input >> std::ws;
                if (input.peek() != '"' || !(input >> std::quoted(text)))
                    throw std::runtime_error("Текст должен быть в двойных кавычках");
                input >> std::ws;
                if (!input.eof() || text.empty() || text.find_first_of("\r\n\x1b") != std::string::npos)
                    throw std::runtime_error("Некорректный текст события");
                const auto start = parseDate(date, time);
                const char repeat = parseRepeat(flag);
                std::ostringstream identity;
                identity << start << ' ' << repeat << ' ' << std::quoted(text);
                const std::string base = identity.str();
                config.events.push_back({base + " #" + std::to_string(++duplicates[base]), start, repeat, text});
            }
        } catch (const std::exception& error) {
            throw std::runtime_error("Строка " + std::to_string(number) + ": " + error.what());
        }
    }
    if (file.bad()) throw std::runtime_error("Ошибка чтения конфигурации");
    return config;
}
}

std::time_t parseDate(const std::string& date, const std::string& time) {
    if (!std::regex_match(date, std::regex("[0-9]{4}-[0-9]{2}-[0-9]{2}")) ||
        !std::regex_match(time, std::regex("[0-9]{2}:[0-9]{2}(:[0-9]{2})?")))
        throw std::runtime_error("Формат: YYYY-MM-DD HH:MM[:SS]");
    std::tm value{};
    std::istringstream input(date + " " + time);
    input >> std::get_time(&value, time.size() == 5 ? "%Y-%m-%d %H:%M" : "%Y-%m-%d %H:%M:%S");
    if (input.fail() || value.tm_year < 70 || value.tm_year > 199 || value.tm_sec > 59)
        throw std::runtime_error("Некорректная дата или время (допустимы 1970–2099)");
    const std::tm original = value;
    value.tm_isdst = -1;
    const auto result = std::mktime(&value);
    if (result == -1 || value.tm_year != original.tm_year || value.tm_mon != original.tm_mon ||
        value.tm_mday != original.tm_mday || value.tm_hour != original.tm_hour ||
        value.tm_min != original.tm_min || value.tm_sec != original.tm_sec)
        throw std::runtime_error("Такой даты или местного времени не существует");
    return result;
}

Config readConfig(const std::string& path) {
    ConfigLock lock(path, LOCK_SH | LOCK_NB);
    return load(path);
}

std::time_t latestOccurrence(const Event& event, std::time_t now) {
    if (event.start > now || event.repeat == '-') return event.start;
    if (event.repeat == 'h') return event.start + ((now - event.start) / 3600) * 3600;
    std::tm start{}, current{};
    if (!localtime_r(&event.start, &start) || !localtime_r(&now, &current))
        throw std::runtime_error("localtime_r failed");
    current.tm_hour = start.tm_hour;
    current.tm_min = start.tm_min;
    current.tm_sec = start.tm_sec;
    if (event.repeat == 'w') current.tm_mday -= (current.tm_wday - start.tm_wday + 7) % 7;
    current.tm_isdst = -1;
    auto candidate = std::mktime(&current);
    if (candidate == -1) throw std::runtime_error("mktime failed");
    if (candidate > now) {
        current.tm_mday -= event.repeat == 'w' ? 7 : 1;
        current.tm_hour = start.tm_hour;
        current.tm_min = start.tm_min;
        current.tm_sec = start.tm_sec;
        current.tm_isdst = -1;
        candidate = std::mktime(&current);
        if (candidate == -1) throw std::runtime_error("mktime failed");
    }
    return candidate < event.start ? event.start : candidate;
}

void addEvent(const std::string& path, const std::vector<std::string>& args) {
    if (args.size() < 3) throw std::runtime_error("add_event YYYY-MM-DD HH:MM[:SS] [-h|-d|-w] text");
    parseDate(args[0], args[1]);
    std::size_t index = 2;
    std::string flag = "-";
    if (args[index] == "-h" || args[index] == "-d" || args[index] == "-w") flag = args[index++];
    else if (args[index].size() == 2 && args[index][0] == '-')
        throw std::runtime_error("Неизвестный флаг повторения");
    std::string text;
    for (; index < args.size(); ++index) {
        if (!text.empty()) text += ' ';
        text += args[index];
    }
    if (text.empty() || text.find_first_of("\r\n\x1b") != std::string::npos)
        throw std::runtime_error("Нужен непустой однострочный текст без ESC");
    ConfigLock lock(path, LOCK_EX);
    load(path);
    std::ofstream file(path, std::ios::app);
    if (!file) throw std::runtime_error("Не открыть конфигурацию для добавления");
    file << '\n' << args[0] << ' ' << args[1] << ' ' << flag << ' ' << std::quoted(text) << '\n';
    file.flush();
    if (!file) throw std::runtime_error("Не записать событие");
}
