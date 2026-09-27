#include "core/daemon.hpp"
#include <iostream>
#include <cstdlib>
#include <syslog.h>

int main(int argc, char* argv[]) {
    openlog("disk_monitor", LOG_PID | LOG_CONS | LOG_NDELAY, LOG_LOCAL0);
    
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path_to_config_file>\n";
        return EXIT_FAILURE;
    }

    std::string configPath = argv[1];

    try {
        auto& daemon = monitor::Daemon::getInstance();
        
        daemon.init(configPath);

        daemon.run();
        
    } catch (const std::exception& e) {
        std::cerr << "Critical error starting daemon: " << e.what() << "\n";
        return EXIT_FAILURE;
    }

    closelog();
    return EXIT_SUCCESS;
}