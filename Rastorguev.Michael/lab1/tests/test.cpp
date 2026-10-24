#include "../src/headers/config.hpp"

#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>

#define RUN(test) run(#test, test)

namespace {

const char* const VERDICT[] = {"FAILED", "PASSED"};


time_t at(int y, int mo, int d, int h, int mi) {
    std::tm t{};
    t.tm_year  = y - 1900;
    t.tm_mon   = mo - 1;
    t.tm_mday  = d;
    t.tm_hour  = h;
    t.tm_min   = mi;
    t.tm_isdst = -1;
    return mktime(&t);
}

std::filesystem::path writeConfig(const char* content) {
    auto path = std::filesystem::temp_directory_path() / "schedule_test.conf";
    std::ofstream(path) << content;
    return path;
}

bool testHourlyAdvancesByOneHour() {
    time_t start = at(2026, 11, 16, 15, 0);

    return advance_time(start, 'h') == at(2026, 11, 16, 16, 0);
}

bool testDailyAdvancesToNextDay() {
    time_t start = at(2026, 11, 16, 15, 0);

    return advance_time(start, 'd') == at(2026, 11, 17, 15, 0);
}

bool testWeeklyAdvancesBySevenDays() {
    time_t start = at(2026, 11, 16, 15, 0);

    return advance_time(start, 'w') == at(2026, 11, 23, 15, 0);
}

bool testLoadParsesFlagAndText() {
    auto path = writeConfig("add_event 2026-11-16 15:00 -w Weekly report\n");
    Config cfg;
    std::string err;

    bool ok = Config::load(path, cfg, err);

    return ok && cfg.events.size() == 1 &&
           cfg.events[0].when == at(2026, 11, 16, 15, 0) &&
           cfg.events[0].repeat == 'w' &&
           cfg.events[0].text == "Weekly report";
}

bool testLoadEventWithoutFlagIsOneShot() {
    auto path = writeConfig("add_event 2026-11-16 15:00 Doctor\n");
    Config cfg;
    std::string err;

    bool ok = Config::load(path, cfg, err);

    return ok && cfg.events.size() == 1 &&
           cfg.events[0].repeat == 0 &&
           cfg.events[0].text == "Doctor";
}

bool testLoadParsesIntervalAndSkipsComments() {
    auto path = writeConfig(
        "add_event 2026-11-16 15:00 -d Workout\n"
        "add_event 2026-11-16 18:00 Dinner\n");
    Config cfg;
    std::string err;

    bool ok = Config::load(path, cfg, err);

    return ok && cfg.interval == 10 && cfg.events.size() == 2;
}

bool run(const char* name, bool (*test)()) {
    std::cout << "[ RUN    ] " << name << '\n';
    bool passed = false;
    try {
        passed = test();
    } catch (const std::exception& e) {
        std::cout << "  exception: " << e.what() << '\n';
    }
    std::cout << "[ " << VERDICT[passed] << " ] " << name << '\n';
    return passed;
}

}

int main() {

    const std::pair<const char*, bool(*)()> tests[] = {
        {"testHourlyAdvancesByOneHour",           testHourlyAdvancesByOneHour},
        {"testDailyAdvancesToNextDay",            testDailyAdvancesToNextDay},
        {"testWeeklyAdvancesBySevenDays",         testWeeklyAdvancesBySevenDays},
        {"testLoadParsesFlagAndText",             testLoadParsesFlagAndText},
        {"testLoadEventWithoutFlagIsOneShot",     testLoadEventWithoutFlagIsOneShot},
        {"testLoadParsesIntervalAndSkipsComments", testLoadParsesIntervalAndSkipsComments},
    };

    int failed = 0;
    for (const auto& [name, fn] : tests)
        failed += !run(name, fn);

    std::cout << "Failed tests: " << failed << '\n';
    return failed;
}