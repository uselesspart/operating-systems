#include "Daemon.hpp"

#include <cstdlib>
#include <iostream>

#include <getopt.h>

namespace {

constexpr int kUsageError = 2;

void showUsage(std::ostream& out, const char* program) {
    out << "Usage: " << program << " [-c CONFIG] [-p PIDFILE] [CONFIG]\n"
        << "Disk monitor daemon: watches the directories listed in CONFIG with inotify\n"
        << "and logs every access to their files to syslog with facility LOG_LOCAL0.\n"
        << "\n"
        << "  -c, --config FILE    config file (default: ./diskmond.conf);\n"
        << "                       may also be given as the only positional argument\n"
        << "  -p, --pidfile FILE   pid file (default: /run/diskmond.pid for root,\n"
        << "                       /tmp/diskmond-<uid>.pid for other users)\n"
        << "  -h, --help           show this help\n"
        << "\n"
        << "Signals: SIGHUP - re-read the config, SIGTERM - log a message and exit.\n";
}

}  

int main(int argc, char* argv[]) {
    diskmond::Options options;
    bool configGiven = false;

    const option longOptions[] = {
        {"config", required_argument, nullptr, 'c'},
        {"pidfile", required_argument, nullptr, 'p'},
        {"help", no_argument, nullptr, 'h'},
        {nullptr, 0, nullptr, 0},
    };

    int choice = 0;
    while ((choice = ::getopt_long(argc, argv, "c:p:h", longOptions, nullptr)) != -1) {
        switch (choice) {
            case 'c':
                options.configFile = optarg;
                configGiven = true;
                break;
            case 'p':
                options.pidFile = optarg;
                break;
            case 'h':
                showUsage(std::cout, argv[0]);
                return EXIT_SUCCESS;
            default:
                showUsage(std::cerr, argv[0]);
                return kUsageError;
        }
    }

    const int positional = argc - optind;
    if (positional > 1 || (positional == 1 && configGiven)) {
        showUsage(std::cerr, argv[0]);
        return kUsageError;
    }
    if (positional == 1) {
        options.configFile = argv[optind];
    }

    return diskmond::Daemon::instance().run(options);
}
