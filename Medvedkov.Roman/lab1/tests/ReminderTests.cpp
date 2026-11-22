#include "ConfigLoader.hpp"
#include "ReminderSchedule.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
unsigned checks = 0;
void check(bool condition) {
    ++checks;
    if (!condition) throw std::runtime_error("Check failed: " + std::to_string(checks));
}
void rejects(const std::string& line) {
    try { reminder::ReminderEvent::parse(line); }
    catch (const std::exception&) { check(true); return; }
    check(false);
}
}

int main(int argc, char* argv[]) {
    try {
        if (argc != 2) throw std::runtime_error("Need temporary config path");
        setenv("TZ", "UTC", 1);
        tzset();
        auto entry = reminder::ReminderEvent::parse("add_event 2026-10-26 12:30 -h Текст $(touch /tmp/no) ' \"");
        check(entry.period == 3600);
        check(entry.message == "Текст $(touch /tmp/no) ' \"");
        for (const auto& flag : {"-d", "-w"}) {
            auto repeated = reminder::ReminderEvent::parse(std::string("add_event 2024-02-29 23:59:59 ") + flag + " test");
            check(repeated.period == (std::string(flag) == "-d" ? 86400U : 604800U));
        }
        for (const auto& line : {
                 "add_event 2025-02-29 12:00 bad", "add_event 2026-04-31 12:00 bad",
                 "add_event 2026-01-01 24:00 bad", "add_event 2026-01-01 12:60 bad",
                 "add_event 2026-01-01 12:00:60 bad", "add_event 2026-01-01 12:00 -x bad",
                 "add_event 2026-01-01 12:00 -h", "add_event 2026-01-01 12:00",
                 "other 2026-01-01 12:00 text"}) rejects(line);
        reminder::Configuration settings;
        settings.entries = {{1000, 0, "once"}, {1000, 3600, "hour"}, {1000, 3600, "hour"}};
        reminder::ReminderSchedule schedule;
        schedule.reload(settings, 999);
        check(schedule.takeDue(999).empty());
        check(schedule.takeDue(1000).size() == 2);
        check(schedule.takeDue(1001).empty());
        schedule.reload(settings, 1001);
        check(schedule.takeDue(1001).empty());
        check(schedule.takeDue(4600).size() == 1);
        check(schedule.takeDue(4601).empty());
        check(schedule.takeDue(20000).size() == 1); // no storm after sleep
        check(schedule.takeDue(20001).empty());
        reminder::ReminderSchedule stale;
        stale.reload(settings, 1200);
        check(stale.takeDue(1200).empty());
        check(stale.takeDue(4600).size() == 1);
        for (unsigned period : {3600U, 86400U, 604800U}) {
            reminder::Configuration repeated;
            repeated.entries = {{1000, period, "repeat"}};
            reminder::ReminderSchedule repeatedSchedule;
            repeatedSchedule.reload(repeated, 1000);
            check(repeatedSchedule.takeDue(1000).size() == 1);
            check(repeatedSchedule.takeDue(1000 + period - 1).empty());
            check(repeatedSchedule.takeDue(1000 + period).size() == 1);
        }
        settings.entries = {{2000, 0, "new"}};
        schedule.reload(settings, 1999);
        check(schedule.takeDue(2000).size() == 1);
        settings.entries.clear();
        schedule.reload(settings, 2001);
        check(schedule.takeDue(100000).empty());
        { std::ofstream config(argv[1]); config << "# comment\ninterval 3\nterminal xterm\nadd_event 2026-10-26 12:30 test\n"; }
        const auto parsed = reminder::ConfigLoader::load(argv[1]);
        check(parsed.interval == 3 && parsed.entries.size() == 1);
        { std::ofstream config(argv[1]); config << "interval 0\n"; }
        bool invalid = false;
        try { reminder::ConfigLoader::load(argv[1]); } catch (const std::exception&) { invalid = true; }
        check(invalid);
        for (const auto& contents : {
                 "interval -1\n", "interval 86401\n", "interval 10 extra\n",
                 "terminal ./relative\n", "terminal\n", "unknown directive\n"}) {
            { std::ofstream config(argv[1]); config << contents; }
            bool rejected = false;
            try { reminder::ConfigLoader::load(argv[1]); }
            catch (const std::exception&) { rejected = true; }
            check(rejected);
        }
        { std::ofstream config(argv[1], std::ios::binary); config << "# CRLF input\r\n\r\nterminal /usr/bin/xterm\r\n"; }
        const auto defaults = reminder::ConfigLoader::load(argv[1]);
        check(defaults.interval == 10 && defaults.entries.empty());
        check(defaults.terminal == "/usr/bin/xterm");
        const auto shortTime = reminder::ReminderEvent::parse("add_event 2026-10-26 12:30 same");
        const auto fullTime = reminder::ReminderEvent::parse("add_event 2026-10-26 12:30:00 same");
        check(shortTime.key() == fullTime.key());
        settings.entries = {{2000, 0, "new"}};
        schedule.reload(settings, 2002);
        check(schedule.takeDue(2002).empty()); // removing/re-adding keeps history
        reminder::ReminderSchedule boundary;
        boundary.reload(settings, 2010);
        check(boundary.takeDue(2010).size() == 1); // last permitted second
        reminder::ReminderSchedule expired;
        expired.reload(settings, 2011);
        check(expired.takeDue(2011).empty());
        std::cout << "Passed " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
