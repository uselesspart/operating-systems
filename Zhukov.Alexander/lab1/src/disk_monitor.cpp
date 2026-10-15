#include "disk_monitor.h"
#include "file_watcher.h"
#include "logger.h"

#include <cstdlib>
#include <iostream>

DiskMonitor& DiskMonitor::instance() {
    static DiskMonitor monitor;
    return monitor;
}

int DiskMonitor::run(const std::filesystem::path& path) {
    configure(path);
    return start();
}

void DiskMonitor::work() {
    FileWatcher watcher(config.paths);
    Logger::info("Config loaded: " + config_path.string());
    ready();
    while (!stopping()) {
        if (reload_requested()) {
            reload(watcher);
        }
        if (stopping()) {
            break;
        }
        watcher.wait(250, stopping);
    }
}

void DiskMonitor::reload(FileWatcher& watcher) {
    if (stopping()) {
        return;
    }
    try {
        auto next_config = load_config(config_path);
        if (stopping()) {
            return;
        }
        if (!watcher.update(next_config.paths, stopping)) {
            return;
        }
        config.paths.swap(next_config.paths);
        Logger::info("Config reloaded: " + config_path.string());
    } catch (const std::exception& error) {
        Logger::error("Reload failed: " + std::string(error.what()));
    }
}

int DiskMonitor::check_config(const std::filesystem::path& path) {
    configure(path);
    Logger::info("Config checked: " + config_path.string());

    for (const auto& directory : config.paths) {
        std::cout << directory.string() << '\n';
    }

    return EXIT_SUCCESS;
}

void DiskMonitor::configure(const std::filesystem::path& path) {
    config_path = std::filesystem::absolute(path);
    config = load_config(config_path);
}
