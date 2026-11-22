#include "logger.h"

#include <syslog.h>

#include <cstdarg>

namespace lab1 {
namespace log {

void open(const char* ident) {
    openlog(ident, LOG_PID, LOG_DAEMON);
}

void info(const char* format, ...) {
    std::va_list arguments;
    va_start(arguments, format);
    vsyslog(LOG_INFO, format, arguments);
    va_end(arguments);
}

void error(const char* format, ...) {
    std::va_list arguments;
    va_start(arguments, format);
    vsyslog(LOG_ERR, format, arguments);
    va_end(arguments);
}

void close() {
    closelog();
}

}  // namespace log
}  // namespace lab1
