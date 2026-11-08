#include "Daemon.hpp"
#include "ArgParser.hpp"

#include <iostream>
#include <exception>
#include <utility>

#include <syslog.h>

int main(int argc, const char *argv[]) {
    auto args = application::parse_args(argc, argv);
    if(!args) {
        std::cerr << "Usage: " << argv[0] << " --config <file>\n";
        return 1;
    }

    ::openlog("disk-monitor", LOG_PID, LOG_LOCAL0);
    try {
        int result = application::Daemon::instance().run(std::move(*args));
        ::closelog();
        return result;
    } catch (const std::exception& error) {
        ::syslog(LOG_ERR, "daemon failed: %s", error.what());
        ::closelog();
        return 1;
    }
}
