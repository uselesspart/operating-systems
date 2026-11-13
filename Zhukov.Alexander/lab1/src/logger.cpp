#include "logger.h"

#include <syslog.h>

Logger::Logger() {
    openlog("disk_monitor", LOG_PID | LOG_NDELAY, LOG_DAEMON);
}

Logger::~Logger() {
    closelog();
}

void Logger::info(const std::string& message) {
    syslog(LOG_DAEMON | LOG_INFO, "%s", message.c_str());
}

void Logger::error(const std::string& message) {
    syslog(LOG_DAEMON | LOG_ERR, "%s", message.c_str());
}

void Logger::event(const std::string& message) {
    syslog(LOG_LOCAL0 | LOG_INFO, "%s", message.c_str());
}
