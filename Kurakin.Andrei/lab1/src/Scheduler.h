#pragma once

#include "Config.h"

#include <ctime>
#include <optional>

// Возвращает последнее событие, которое уже должно сработать.
std::optional<std::time_t> currentOccurrence(const ReminderEvent& event,
                                              std::time_t now);
