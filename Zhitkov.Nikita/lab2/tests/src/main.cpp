#include "test_scenario.h"
#include "test_protocol.h"
#include "test_sems.h"
#include <iostream>

#define RUN_TEST(fn, label) \
    do { \
        fn(); \
        std::cout << label << ": PASSED\n"; \
    } while (0)

int main() {
    RUN_TEST(testScenario, "testScenario");
    RUN_TEST(testProtocol, "testProtocol");
    RUN_TEST(testSems,     "testSems");
    std::cout << "Unit tests PASSED\n";
    return 0;
}