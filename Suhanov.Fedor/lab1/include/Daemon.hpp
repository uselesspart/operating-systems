#pragma once

#include "ArgParser.hpp"
#include "Monitor.hpp"

#include <memory>

namespace application {

class Daemon {
private:
    Path config_path_;
    Path start_dir_;
    Path pid_path_;
    AppConfig config_;
    std::unique_ptr<Monitor> monitor_;
    int signal_fd_{-1};

    Daemon() = default;

    bool reload();
    bool stop_previous();
    bool write_pid();
    void remove_pid();
    int event_loop();

public:
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
    Daemon(Daemon&&) = delete;
    Daemon& operator=(Daemon&&) = delete;

    static Daemon& instance();
    int run(Arguments args);
};

} // namespace application

