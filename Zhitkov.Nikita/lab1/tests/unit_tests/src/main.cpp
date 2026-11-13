#include "test_config.h"
#include "test_events.h"

#include <iostream>

#define RUN_TEST(fn) \
    do { \
        std::cout << "  " << #fn << " " << std::flush; \
        fn(); \
        std::cout << "ok\n"; \
    } while (0)

int main() {
    // Config
    std::cout << "Config:\n";
    testValidConfig();
    testWhitespaceTolerance();
    testMissingEventsFile();
    testInvalidInterval();
    testNegativeInterval();
    testZeroInterval();
    testMissingFile();
    std::cout << "test_config: PASSED\n";

    // Event
    std::cout << "Event:\n";
    testParseEventLine();
    testOneTimeEvent();
    testHourlyEvent();
    testDailyEvent();
    testWeeklyEvent();
    testRecurringScheduleAlignment();;
    testGraceVariants();
    std::cout << "test_events: PASSED\n";

    return 0;
}