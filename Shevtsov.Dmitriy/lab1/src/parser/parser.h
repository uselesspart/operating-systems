#pragma once

#include <string>
#include <vector>

struct Task;

std::vector<Task> loadConfig(const std::string& path);
