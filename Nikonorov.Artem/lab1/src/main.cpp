#include "disk_monitor/Daemon.hpp"
#include "disk_monitor/Logging.hpp"

#include <iostream>
#include <string>
#include <utility>
#include <syslog.h>

namespace {

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [--foreground] [--config PATH] [--pid-file PATH]\n"
              << "       " << program << " --help\n\n"
              << "Options:\n"
              << "  --foreground     run without daemonizing (useful for development/tests)\n"
              << "  --config PATH    configuration file (default: ./disk-monitor.conf)\n"
              << "  --pid-file PATH  PID file (default: /run/disk-monitor.pid); a .lock file is kept\n"
              << "  --help           show this help\n";
}

bool parseArguments(int argc, char** argv, diskmon::CommandLineOptions& options,
                   std::string& error, bool& helpRequested) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument(argv[index]);
        if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            helpRequested = true;
            return false;
        }
        if (argument == "--foreground") {
            options.foreground = true;
            continue;
        }
        if (argument == "--config" || argument == "--pid-file") {
            if (index + 1 >= argc) {
                error = "Missing value for " + argument;
                return false;
            }
            const std::string value(argv[++index]);
            if (value.empty()) {
                error = "Empty value for " + argument;
                return false;
            }
            if (argument == "--config") {
                options.configPath = value;
            } else {
                options.pidPath = value;
            }
            continue;
        }
        error = "Unknown argument: " + argument;
        printUsage(argv[0]);
        return false;
    }
    return true;
}

} 

int main(int argc, char** argv) {
    diskmon::CommandLineOptions options;
    std::string error;
    bool helpRequested = false;
    if (!parseArguments(argc, argv, options, error, helpRequested)) {
        if (helpRequested) {
            return 0;
        }
        diskmon::initializeLogging(false);
        diskmon::logDaemon(LOG_ERR, error);
        std::cerr << error << '\n';
        diskmon::closeLogging();
        return 2;
    }
    return diskmon::Daemon::instance().run(options);
}
