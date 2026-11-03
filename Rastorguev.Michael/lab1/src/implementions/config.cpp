#include "../headers/config.hpp"

#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <syslog.h>

namespace {

std::string trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b])))
    {
        ++b; 
    } 

    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) 
    {
        --e;
    }

    return s.substr(b, e - b);
}

bool parse_event(std::istringstream& iss, Event& ev, std::string& err) {
    std::string date, tm_str;
    if (!(iss >> date >> tm_str)) {
        err = "ожидается: add_event дата время [флаг] текст";
        return false;
    }

    int y, mo, d, h, mi;
    char extra;
    if (std::sscanf(date.c_str(), "%d-%d-%d%c", &y, &mo, &d, &extra) != 3) {
        err = "неверная дата '" + date + "' (нужно YYYY-MM-DD)";
        return false;
    }
    if (std::sscanf(tm_str.c_str(), "%d:%d%c", &h, &mi, &extra) != 2) {
        err = "неверное время '" + tm_str + "' (нужно HH:MM)";
        return false;
    }

    std::tm t{};
    t.tm_year  = y - 1900;
    t.tm_mon   = mo - 1;
    t.tm_mday  = d;
    t.tm_hour  = h;
    t.tm_min   = mi;
    t.tm_isdst = -1;
    time_t when = mktime(&t);


    std::string rest;
    std::getline(iss, rest);
    rest = trim(rest);

    char repeat = 0;
    if (rest.size() >= 2 && rest[0] == '-' &&
        (rest[1] == 'h' || rest[1] == 'd' || rest[1] == 'w') &&
        (rest.size() == 2 || std::isspace(static_cast<unsigned char>(rest[2])))) {
        repeat = rest[1];
        rest = trim(rest.substr(2));
    }

    if (rest.empty()) {
        err = "пустой текст напоминания";
        return false;
    }

    ev.when   = when;
    ev.repeat = repeat;
    ev.text   = rest;
    return true;
}

} 

time_t advance_time(time_t t, char repeat) {
    if (repeat == 'h') return t + 3600;

    std::tm tmv{};
    localtime_r(&t, &tmv);
    tmv.tm_mday += (repeat == 'w') ? 7 : 1;
    tmv.tm_isdst = -1;
    time_t r = mktime(&tmv);
    if (r <= t) r = t + 3600;
    return r;
}

bool Config::load(const std::filesystem::path& path, Config& out, std::string& err) {
    std::ifstream file(path);
    if (!file.is_open()) {
        err = "не удалось открыть конфигурационный файл " + path.string();
        return false;
    }

    Config cfg;
    std::string line;
    int lineno = 0;
    while (std::getline(file, line)) {
        lineno++;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;

        std::istringstream iss(line);
        std::string cmd;
        iss >> cmd;

        std::string perr;
        if (cmd == "add_event") {
            Event ev;
            if (parse_event(iss, ev, perr)) {
                cfg.events.push_back(ev);
            } else {
                syslog(LOG_WARNING, "%s:%d: %s", path.c_str(), lineno, perr.c_str());
            }
        }
    }

    out = std::move(cfg);
    return true;
}
