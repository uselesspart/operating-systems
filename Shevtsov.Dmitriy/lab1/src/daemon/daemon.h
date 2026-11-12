#pragma once

#include <string>
#include <vector>

struct Task;

class Daemon{
    private:
        std::string configPath;
        std::vector<Task> tasks;

        Daemon();
        ~Daemon();

        bool reloadConfig();
        void daemonize();
        void takePidFile();

    public:
        static Daemon& instance();

        Daemon(const Daemon&) = delete;
        Daemon& operator=(const Daemon&) = delete;

        int run(const std::string& configFile);
};
