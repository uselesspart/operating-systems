#pragma once

#include <chrono>
#include <string>

enum class Repeat
{
    None,
    Hourly,
    Daily,
    Weekly
};

struct ReminderEvent
{
    // внутренний идентификатор события.
    std::string key;

    // время первого срабатывания.
    std::chrono::system_clock::time_point time;

    Repeat repeat = Repeat::None;

    std::string text;
};