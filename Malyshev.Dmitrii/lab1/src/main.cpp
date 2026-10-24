#include <iostream>
#include <string>

#include "Daemon.hpp"

int main(int argc, char* argv[]) {
    if (argc > 2 || (argc == 2 && (std::string(argv[1]) == "-h" || std::string(argv[1]) == "--help"))) {
        std::cerr << "Usage: " << argv[0] << " [config_file]\n"
                  << "Default config: ./config.conf\n";
        return argc > 2 ? 1 : 0;
    }
    const std::string config = (argc == 2) ? argv[1] : "config.conf";
    return Daemon::instance().run(config);
}
