#pragma once

#include <chrono>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

inline unsigned checks = 0;
inline void expect(bool condition, const std::string& description) {
    ++checks;
    if (!condition) throw std::runtime_error(description);
}
template<class Function> void rejects(Function function, const std::string& description) {
    bool rejected = false;
    try { function(); } catch (const std::exception&) { rejected = true; }
    expect(rejected, description);
}
template<class Function> void eventually(Function condition, const std::string& description) {
    const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(6);
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= limit) throw std::runtime_error(description);
        usleep(1000);
    }
    expect(true, description);
}
