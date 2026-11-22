#pragma once

#include "Config.hpp"
#include "ReminderEvent.hpp"

#include <chrono>
#include <ctime>
#include <optional>
#include <string>
#include <unordered_map>


class ReminderTask
{
public:
    void execute(const Config& config);

private:
    std::optional<std::chrono::system_clock::time_point>
    getCurrentOccurrence(
        const ReminderEvent& event,
        std::chrono::system_clock::time_point now
    ) const;

    void showReminder(
        const ReminderEvent& event
    ) const;


    // event.key -> время последнего уже показанного occurrence.
    std::unordered_map<std::string, std::time_t> lastFired_;
};