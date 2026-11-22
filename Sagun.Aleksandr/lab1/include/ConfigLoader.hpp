#pragma once

#include "Config.hpp"

#include <filesystem>
#include <chrono>
#include <string>

class ConfigLoader
{
public:
    static Config load(const std::filesystem::path& path);

private:
    static std::chrono::system_clock::time_point parseDateTime(
        const std::string& data,
        const std::string& time
    );

    static std::string trim(
        const std::string& value
    );
};
