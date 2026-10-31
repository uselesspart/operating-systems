#include "Daemon.h"

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    if (argc > 2 || (argc == 2 && std::string(argv[1]) == "--help")) {
        std::cout << "Usage: " << argv[0] << " [config-file]\n";
        return argc > 2 ? 2 : 0;
    }

    const std::string config_file = argc == 2 ? argv[1] : "disk_monitor.conf";
    return disk_monitor::Daemon::instance().run(config_file);
}

