#pragma once

#include <filesystem>

namespace disk_monitor {

class Daemon {
public:
    static Daemon& instance();

    int run(const std::filesystem::path& config_path);

private:
    Daemon() = default;
    Daemon(const Daemon&) = delete;
    Daemon& operator=(const Daemon&) = delete;
};

}  // namespace disk_monitor

