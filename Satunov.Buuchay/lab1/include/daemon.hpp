#pragma once

#include <filesystem>

class Daemon {
public:
    static int run(const std::filesystem::path& config_path);

private:
    static bool daemonize();
};