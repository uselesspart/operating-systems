#pragma once

#include <filesystem>
#include <string>

#include "config.hpp"

namespace dirclean {

// Демон существует в единственном экземпляре, поэтому класс - синглтон.
class Daemon {
public:
    static Daemon& instance();

    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    Daemon(Daemon&&) = delete;
    Daemon& operator=(Daemon&&) = delete;

    int run(const std::string& configPath);

    const std::filesystem::path& configPath() const { return configPath_; }
    const Config& config() const { return config_; }

private:
    Daemon() = default;

    void refreshConfig();
    void daemonize();
    void mainLoop();
    void doWork();

    std::filesystem::path configPath_;
    Config config_;
    int intervalSeconds_ = 20;
};

}
