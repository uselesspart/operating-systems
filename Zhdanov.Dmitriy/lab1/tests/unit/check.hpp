#pragma once

#include <iostream>

inline int& failureCount() {
    static int count = 0;
    return count;
}

#define CHECK(condition) \
    do { \
        if (!(condition)) { \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " #condition << std::endl; \
            ++failureCount(); \
        } \
    } while (false)

#define CHECK_EQ(actual, expected) \
    do { \
        if (!((actual) == (expected))) { \
            std::cerr << __FILE__ << ":" << __LINE__ << ": " #actual " != " #expected \
                      << std::endl; \
            ++failureCount(); \
        } \
    } while (false)

inline int finishTests() {
    if (failureCount() == 0) {
        std::cout << "OK" << std::endl;
        return 0;
    }
    std::cerr << "Провалено проверок: " << failureCount() << std::endl;
    return 1;
}
