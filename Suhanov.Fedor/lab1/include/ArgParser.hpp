#pragma once

#include <filesystem>
#include <optional>

namespace application
{

using Path = std::filesystem::path;

struct Arguments {
    Path config;
};

std::optional<Arguments> parse_args(int argc, const char* argv[]);
    
} // namespace application
