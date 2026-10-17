#pragma once

#include <chrono>
#include <optional>
#include <string>

enum class flagValue{
    ONCE,
    HOURLY,
    DAILY,
    WEEKLY,
    SIZE
};

struct Task{
    std::chrono::sys_seconds day;
    flagValue flag = flagValue::ONCE;
    std::string text;
};

inline std::optional<std::chrono::sys_seconds> nextOccurrence(const Task& task, std::chrono::sys_seconds after){
    using namespace std::chrono;
    if (task.day > after) return task.day;

    seconds period;
    switch (task.flag) {
        case flagValue::HOURLY: period = hours(1); break;
        case flagValue::DAILY:  period = days(1);  break;
        case flagValue::WEEKLY: period = weeks(1); break;
        default: return std::nullopt;
    }
    return task.day + ((after - task.day) / period + 1) * period;
}
