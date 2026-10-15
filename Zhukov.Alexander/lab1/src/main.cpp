#include "disk_monitor.h"
#include "logger.h"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>

int main(int argc, char* argv[]) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << "Usage: " << argv[0] << " <config.yaml>\n";
        std::cout << "       " << argv[0] << " --check-config <config.yaml>\n";
        return EXIT_SUCCESS;
    }

    const bool check_only = argc == 3 && std::string_view(argv[1]) == "--check-config";
    if (!check_only && (argc != 2 || std::string_view(argv[1]).substr(0, 2) == "--")) {
        std::cerr << "Usage: " << argv[0] << " [--check-config] <config.yaml>\n";
        return EXIT_FAILURE;
    }

    Logger logger;
    try {
        auto& monitor = DiskMonitor::instance();
        if (check_only) {
            return monitor.check_config(argv[2]);
        }
        return monitor.run(argv[1]);
    } catch (const std::exception& error) {
        Logger::error(error.what());
        std::cerr << "Error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
