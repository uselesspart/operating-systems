#include "daemon.hpp"

#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config-file>\n";
        return 1;
    }

    return Daemon::instance().run(argv[1]);
}