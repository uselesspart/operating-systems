#include "test_config.h"
#include "test_events.h"

#include <iostream>

int main() {
    // Config
    testValidConfig();
    testWhitespaceTolerance();
    testMissingEventsFile();
    testInvalidInterval();
    testNegativeInterval();
    testZeroInterval();
    testMissingFile();
    std::cout << "test_config: PASSED\n";

    // Event
    testParseEventLine();
    testOneTimeEvent();
    testHourlyEvent();
    testDailyEvent();
    testWeeklyEvent();
    testGraceVariants();
    std::cout << "test_events: PASSED\n";

    return 0;
}