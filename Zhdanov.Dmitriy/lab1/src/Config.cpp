#include "Config.hpp"

#include <cstdio>
#include <fstream>

bool loadEvents(const std::string& path, std::vector<Event>& events, std::string& error) {
    std::ifstream input(path);
    if (!input) {
        error = "не удалось открыть файл событий " + path;
        return false;
    }

    std::vector<Event> loaded;
    std::string line;
    int lineNumber = 0;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        std::size_t start = line.find_first_not_of(" \t");
        if (start == std::string::npos) {
            continue;
        }
        if (line[start] == '#') {
            continue;
        }

        Event event;
        if (!parseEvent(line, event)) {
            error = path + ":" + std::to_string(lineNumber) + ": неверная строка события";
            return false;
        }
        loaded.push_back(event);
    }

    if (input.bad()) {
        error = "ошибка чтения файла событий " + path;
        return false;
    }

    events = loaded;
    return true;
}

bool saveEvents(const std::string& path, const std::vector<Event>& events, std::string& error) {
    std::string temporary = path + ".tmp";
    {
        std::ofstream output(temporary, std::ios::trunc);
        if (!output) {
            error = "не удалось открыть " + temporary;
            return false;
        }
        for (const Event& event : events) {
            output << formatEvent(event) << '\n';
        }
        output.close();
        if (!output) {
            std::remove(temporary.c_str());
            error = "не удалось записать " + temporary;
            return false;
        }
    }

    if (std::rename(temporary.c_str(), path.c_str()) != 0) {
        std::remove(temporary.c_str());
        error = "не удалось заменить " + path;
        return false;
    }
    return true;
}
