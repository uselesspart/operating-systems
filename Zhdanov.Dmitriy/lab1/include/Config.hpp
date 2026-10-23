#pragma once

#include "Event.hpp"

#include <string>
#include <vector>

bool loadEvents(const std::string& path, std::vector<Event>& events, std::string& error);
bool saveEvents(const std::string& path, const std::vector<Event>& events, std::string& error);
