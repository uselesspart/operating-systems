#include "parser/parser.h"
#include "types/task.h"

#include <filesystem>
#include <fstream>
#include <iostream>

#define RUN(test) run(#test, test)

using namespace std::chrono;

namespace {

const sys_seconds START = sys_days{2026y / October / 5} + 9h;
const char* const VERDICT[] = {"FAILED", "PASSED"};

bool testOnceTaskInFutureReturnsItsTime(){
    Task task{START, flagValue::ONCE, "Doctor"};

    auto next = nextOccurrence(task, START - 1h);

    return next == START;
}

bool testOnceTaskInPastReturnsNothing(){
    Task task{START, flagValue::ONCE, "Doctor"};

    auto next = nextOccurrence(task, START + 1h);

    return !next.has_value();
}

bool testDailyTaskInPastReturnsNextDay(){
    Task task{START, flagValue::DAILY, "Workout"};

    auto next = nextOccurrence(task, START + 3h);

    return next == START + days(1);
}

bool testLoadConfigParsesFlagAndText(){
    auto path = std::filesystem::temp_directory_path() / "reminder_test.conf";
    std::ofstream(path) << "add_event 2026-10-05 09:00 -w Weekly report\n";

    auto tasks = loadConfig(path.string());

    return tasks.size() == 1 && tasks[0].flag == flagValue::WEEKLY && tasks[0].text == "Weekly report";
}

bool run(const char* name, bool (*test)()){
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

int main(){
    int failed = 0;
    failed += !RUN(testOnceTaskInFutureReturnsItsTime);
    failed += !RUN(testOnceTaskInPastReturnsNothing);
    failed += !RUN(testDailyTaskInPastReturnsNextDay);
    failed += !RUN(testLoadConfigParsesFlagAndText);

    std::cout << "Failed tests: " << failed << '\n';
    return failed;
}
