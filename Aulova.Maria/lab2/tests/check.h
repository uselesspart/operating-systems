#pragma once

#include <cstdio>

inline int g_failures = 0;

#define CHECK(condition)                                                          \
    do {                                                                          \
        if (!(condition)) {                                                       \
            std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition);    \
            ++g_failures;                                                         \
        }                                                                         \
    } while (0)

#define RUN(test)                                                                 \
    do {                                                                          \
        const int failuresBefore = g_failures;                                    \
        test();                                                                   \
        std::printf("[%s] %s\n", g_failures == failuresBefore ? "PASS" : "FAIL", #test); \
    } while (0)
