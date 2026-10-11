#include <cstdio>
#include <exception>
#include <string>

#include "Daemon.h"

int main(int argc, char* argv[]) {
    std::string configPath = "reminder.conf";
    if (argc > 1) configPath = argv[1];

    try {
        return Daemon::instance().start(configPath) ? 0 : 1;
    } catch (const std::exception& e) {
        std::fprintf(stderr, "Fatal error: %s\n", e.what());
        return 1;
    }
}