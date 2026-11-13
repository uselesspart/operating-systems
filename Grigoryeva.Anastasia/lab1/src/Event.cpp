#include "Event.h"

#include <cctype>
#include <cstring>
#include <fstream>
#include <sstream>

namespace {

std::string trim(const std::string& s) {
    const char* ws = " \t\r\n";
    std::size_t b = s.find_first_not_of(ws);
    if (b == std::string::npos) return "";
    std::size_t e = s.find_last_not_of(ws);
    return s.substr(b, e - b + 1);
}

bool allDigits(const std::string& s, std::size_t from, std::size_t len) {
    for (std::size_t i = from; i < from + len; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
    }
    return true;
}

bool parseDate(const std::string& s, int& y, int& m, int& d) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    if (!allDigits(s, 0, 4) || !allDigits(s, 5, 2) || !allDigits(s, 8, 2)) return false;
    y = std::stoi(s.substr(0, 4));
    m = std::stoi(s.substr(5, 2));
    d = std::stoi(s.substr(8, 2));
    return true;
}

bool parseTime(const std::string& s, int& h, int& mi, int& sec) {
    if (s.size() != 5 && s.size() != 8) return false;
    if (s[2] != ':' || !allDigits(s, 0, 2) || !allDigits(s, 3, 2)) return false;
    h = std::stoi(s.substr(0, 2));
    mi = std::stoi(s.substr(3, 2));
    sec = 0;
    if (s.size() == 8) {
        if (s[5] != ':' || !allDigits(s, 6, 2)) return false;
        sec = std::stoi(s.substr(6, 2));
    }
    return true;
}

// n-е срабатывание события (n = 0 - первое).
// Для -d и -w используется календарная арифметика, чтобы время суток не "плыло" при смене сезонного времени.
std::time_t occurrence(const Event& ev, long n) {
    if (n == 0) return ev.start;
    if (ev.repeat == Repeat::Hourly) return ev.start + n * 3600;
    std::tm t;
    localtime_r(&ev.start, &t);
    t.tm_mday += static_cast<int>(n) * (ev.repeat == Repeat::Daily ? 1 : 7);
    t.tm_isdst = -1;
    return std::mktime(&t);
}

long periodSeconds(Repeat r) {
    switch (r) {
        case Repeat::Hourly: return 3600;
        case Repeat::Daily: return 86400;
        case Repeat::Weekly: return 7 * 86400;
        default: return 0;
    }
}

}  // namespace

std::string Event::key() const {
    return std::to_string(static_cast<long long>(start)) + "|" +
           std::to_string(static_cast<int>(repeat)) + "|" + text;
}

bool parseEventLine(const std::string& line, Event& out, std::string& error) {
    std::istringstream in(line);
    std::string dateStr, timeStr;
    if (!(in >> dateStr >> timeStr)) {
        error = "ожидается формат: ДАТА ВРЕМЯ [-h|-d|-w] ТЕКСТ";
        return false;
    }

    int y = 0, mo = 0, d = 0, h = 0, mi = 0, s = 0;
    if (!parseDate(dateStr, y, mo, d)) {
        error = "неверная дата '" + dateStr + "' (нужен формат ГГГГ-ММ-ДД)";
        return false;
    }
    if (!parseTime(timeStr, h, mi, s)) {
        error = "неверное время '" + timeStr + "' (нужен формат ЧЧ:ММ или ЧЧ:ММ:СС)";
        return false;
    }

    std::string rest;
    std::getline(in, rest);
    rest = trim(rest);

    Repeat repeat = Repeat::None;
    std::size_t sp = rest.find_first_of(" \t");
    std::string first = rest.substr(0, sp);
    if (first == "-h" || first == "-d" || first == "-w") {
        repeat = (first == "-h") ? Repeat::Hourly : (first == "-d") ? Repeat::Daily : Repeat::Weekly;
        rest = (sp == std::string::npos) ? "" : trim(rest.substr(sp));
    }
    if (rest.empty()) {
        error = "пустой текст напоминания";
        return false;
    }

    if (mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59 || s > 59) {
        error = "дата или время вне допустимого диапазона";
        return false;
    }
    std::tm t;
    std::memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900;
    t.tm_mon = mo - 1;
    t.tm_mday = d;
    t.tm_hour = h;
    t.tm_min = mi;
    t.tm_sec = s;
    t.tm_isdst = -1;
    std::time_t tt = std::mktime(&t);
    // mktime "нормализует" несуществующие даты (30 февраля -> 2 марта); отлавливаем это
    if (tt == NO_OCCURRENCE || t.tm_year != y - 1900 || t.tm_mon != mo - 1 || t.tm_mday != d ||
        t.tm_hour != h || t.tm_min != mi) {
        error = "несуществующая дата или время: " + dateStr + " " + timeStr;
        return false;
    }

    out.start = tt;
    out.repeat = repeat;
    out.text = rest;
    return true;
}

std::time_t firstOccurrenceAfter(const Event& ev, std::time_t after) {
    if (ev.start > after) return ev.start;
    if (ev.repeat == Repeat::None) return NO_OCCURRENCE;
    long n = static_cast<long>((after - ev.start) / periodSeconds(ev.repeat));
    if (n > 0) --n;  // запас на смену сезонного времени
    while (occurrence(ev, n) <= after) ++n;
    return occurrence(ev, n);
}

void loadEvents(std::istream& in, std::vector<Event>& events, std::vector<std::string>& errors) {
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        line = trim(line);
        if (line.empty() || line[0] == '#') continue;
        Event ev;
        std::string err;
        if (parseEventLine(line, ev, err)) {
            events.push_back(ev);
        } else {
            errors.push_back("строка " + std::to_string(lineNo) + ": " + err);
        }
    }
}

bool loadEventsFromFile(const std::string& path, std::vector<Event>& events,
                        std::vector<std::string>& errors) {
    std::ifstream f(path);
    if (!f) {
        errors.push_back("не удалось открыть файл " + path);
        return false;
    }
    loadEvents(f, events, errors);
    return true;
}
