#pragma once

#include <string>

class Daemon {
public:
    static Daemon& instance();
    int run(const std::string& configPath);

private:
    Daemon() = default;
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;

    bool daemonize();
};
