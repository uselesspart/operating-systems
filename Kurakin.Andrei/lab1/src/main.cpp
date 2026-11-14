#include "ReminderDaemon.h"

#include <syslog.h>

#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Usage: " << argv[0] << " [config-file]\n";
        return 1;
    }
    try {
        const char* config = argc == 2 ? argv[1] : "config.conf";
        return ReminderDaemon::instance().start(config);
    } catch (const std::exception& error) {
        ::openlog("reminder_daemon", LOG_PID | LOG_NDELAY, LOG_DAEMON);
        ::syslog(LOG_ERR, "Startup error: %s", error.what());
        ::closelog();
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
