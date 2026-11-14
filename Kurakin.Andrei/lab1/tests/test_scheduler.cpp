#include "Scheduler.h"

#include <cassert>
#include <cstdlib>
#include <ctime>
#include <iostream>

namespace {

std::time_t localTime(int year, int month, int day, int hour, int minute) {
    std::tm time = {};
    time.tm_year = year - 1900;
    time.tm_mon = month - 1;
    time.tm_mday = day;
    time.tm_hour = hour;
    time.tm_min = minute;
    time.tm_isdst = -1;
    return std::mktime(&time);
}

void expect(const ReminderEvent& event, std::time_t now, std::time_t expected) {
    const auto actual = currentOccurrence(event, now);
    assert(actual.has_value());
    assert(*actual == expected);
}

} // конец анонимного пространства имён

int main() {

    (void)::setenv("TZ", "Europe/Berlin", 1);
    ::tzset();

    assert(localTime(2026, 10, 25, 8, 15) -
           localTime(2026, 10, 24, 8, 15) == 25 * 3600);

    ReminderEvent event;
    event.firstDue = localTime(2026, 10, 24, 8, 15);
    assert(!currentOccurrence(event, localTime(2026, 10, 24, 8, 14)).has_value());
    expect(event, localTime(2026, 10, 26, 9, 0), event.firstDue);

    event.repeat = Repeat::Hourly;
    expect(event, localTime(2026, 10, 24, 10, 16),
           localTime(2026, 10, 24, 10, 15));
    expect(event, localTime(2026, 10, 24, 11, 15),
           localTime(2026, 10, 24, 11, 15));

    event.repeat = Repeat::Daily;
    expect(event, localTime(2026, 10, 25, 8, 16),
           localTime(2026, 10, 25, 8, 15));
    expect(event, localTime(2026, 10, 26, 8, 14),
           localTime(2026, 10, 25, 8, 15));
    expect(event, localTime(2026, 10, 26, 8, 15),
           localTime(2026, 10, 26, 8, 15));

    event.repeat = Repeat::Weekly;
    expect(event, localTime(2026, 10, 31, 8, 14),
           localTime(2026, 10, 24, 8, 15));
    expect(event, localTime(2026, 10, 31, 8, 15),
           localTime(2026, 10, 31, 8, 15));
    expect(event, localTime(2026, 11, 1, 8, 16),
           localTime(2026, 10, 31, 8, 15));

    std::cout << "Recurrence scheduling tests passed.\n";
    return 0;
}
