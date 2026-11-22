#include "parser.h"

#include "types/task.h"

#include <ctime>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace {

Task parseTask(std::istringstream& in){
    std::string date, time, word, text;
    in >> date >> time >> word;
    std::getline(in, text);

    std::tm tm{};
    std::string dateTime = date + " " + time;
    const char* end = strptime(dateTime.c_str(), "%Y-%m-%d %H:%M", &tm);
    if (!end || *end != '\0') throw std::runtime_error("bad date/time: " + dateTime);
    tm.tm_isdst = -1;

    Task task;
    task.day = std::chrono::sys_seconds(std::chrono::seconds(std::mktime(&tm)));

    if (word == "-h") task.flag = flagValue::HOURLY;
    else if (word == "-d") task.flag = flagValue::DAILY;
    else if (word == "-w") task.flag = flagValue::WEEKLY;
    else text = word + text;

    text.erase(0, text.find_first_not_of(' '));
    if (text.empty()) throw std::runtime_error("empty reminder text");
    task.text = text;
    return task;
}

}

std::vector<Task> loadConfig(const std::string& path){
    std::ifstream file(path);
    if (!file) throw std::runtime_error("cannot open " + path);

    std::vector<Task> tasks;
    std::string line;
    for (int n = 1; std::getline(file, line); ++n) {
        std::istringstream in(line);
        std::string key;
        in >> key;
        if (key.empty() || key[0] == '#') continue;

        try {
            if (key != "add_event") throw std::runtime_error("unknown keyword: " + key);
            tasks.push_back(parseTask(in));
        } catch (const std::runtime_error& e) {
            throw std::runtime_error("line " + std::to_string(n) + ": " + e.what());
        }
    }
    return tasks;
}
