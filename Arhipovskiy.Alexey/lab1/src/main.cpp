#include "DiskMonitor.h"

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

constexpr const char* kDefaultConfig = "disk_monitor.conf";

void printUsage(const char* program)
{
    std::cout << "Usage: " << program << " [config]\n"
              << "  config  path to the config file (default: ./" << kDefaultConfig << ")\n"
              << "Signals: SIGHUP - reload config, SIGTERM - stop the daemon\n";
}

} // namespace

int main(int argc, char* argv[])
{
    if (argc > 2) {
        printUsage(argv[0]);
        return EXIT_FAILURE;
    }

    const std::string argument = argc == 2 ? argv[1] : "";
    if (argument == "-h" || argument == "--help") {
        printUsage(argv[0]);
        return EXIT_SUCCESS;
    }

    return diskmon::DiskMonitor::instance().run(argument.empty() ? kDefaultConfig : argument);
}
