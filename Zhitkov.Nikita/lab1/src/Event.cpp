#include "Event.h"

#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

std::time_t Event::repeatSeconds() const {
    switch (flag) {
        case 'h': return 3600;
        case 'd': return 86400;
        case 'w': return 604800;
        default:  return 0;
    }
}

namespace {

void ltrim(std::string& s) {
    std::size_t i = 0;
    while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
    s.erase(0, i);
}

}

bool parseEventLine(const std::string& line, Event& out) {
    if (line.size() < 19) return false;

    std::tm tm{};
    std::istringstream ss(line.substr(0, 19));
    ss >> std::get_time(&tm, "%d.%m.%Y %H:%M:%S");
    if (ss.fail()) return false;

    tm.tm_isdst = -1;
    const std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1)) return false;

    std::string rest = line.substr(19);
    ltrim(rest);

    char flag = 0;
    if (rest.size() >= 2 && rest[0] == '-' &&
        (rest[1] == 'h' || rest[1] == 'd' || rest[1] == 'w')) {
        if (rest.size() == 2 || std::isspace(static_cast<unsigned char>(rest[2]))) {
            flag = rest[1];
            rest = rest.substr(2);
            ltrim(rest);
        }
    }

    out.time      = t;
    out.flag      = flag;
    out.text      = rest;
    out.lastFired = 0;
    return true;
}

std::vector<Event> parseEventsFile(const std::string& path) {
    std::vector<Event> events;
    std::ifstream in(path);
    if (!in) return events;  // файла может ещё не быть

    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        Event ev;
        if (parseEventLine(line, ev)) {
            events.push_back(ev);
        }
    }
    return events;
}

EventAction decideEventAction(const Event& ev,
                              std::time_t lastFired,
                              std::time_t now,
                              std::time_t grace,
                              std::time_t* outDue) {
    if (ev.flag == 0 && lastFired != 0) {
        if (outDue) *outDue = 0;
        return EventAction::Skip;
    }

    std::time_t due = (lastFired == 0) ? ev.time : lastFired + ev.repeatSeconds();
    const std::time_t r = ev.repeatSeconds();

    // Для повторяющихся: если мы уже за пределами grace, продвигаем due
    // на целое число периодов вперёд, потому что расписание повторяющегося события - это дискретный набор точек
    if (r > 0 && now > due + grace) {
        const std::time_t missed = now - due;
        const std::time_t periods = missed / r;
        if (periods > 0) due += periods * r;
    }

    if (outDue) *outDue = due;

    if (now < due) return EventAction::Skip;
    if (now - due > grace) return EventAction::MarkStale;
    return EventAction::Fire;
}