#pragma once

#include "daemon.h"
#include "config.h"

class FileWatcher;

class DiskMonitor final : public Daemon {
public:
    static DiskMonitor& instance();

    int run(const std::filesystem::path& config_path) override;
    int check_config(const std::filesystem::path& config_path);

private:
    DiskMonitor() = default;

    void configure(const std::filesystem::path& path);
    void work() override;
    void reload(FileWatcher& watcher);

    std::filesystem::path config_path;
    Config config;
};
