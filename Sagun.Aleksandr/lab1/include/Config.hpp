#pragma once

#include "ReminderEvent.hpp"

#include <vector>
#include <chrono>


struct Config{

    //список событий
    std::vector<ReminderEvent> events;

    std::chrono::seconds interval{10};
};