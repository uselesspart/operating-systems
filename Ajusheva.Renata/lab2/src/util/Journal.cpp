#include "util/Journal.hpp"

#include <syslog.h>

namespace chatlab::journal {

namespace {

void write(int severity, const std::string& text) {
    ::syslog(LOG_USER | severity, "%s", text.c_str());
}

}

void open(const char* ident, Echo echo) {
    const int flags = LOG_PID | (echo == Echo::Stderr ? LOG_PERROR : 0);
    ::openlog(ident, flags, LOG_USER);
}

void close() {
    ::closelog();
}

void info(const std::string& text) {
    write(LOG_INFO, text);
}

void warn(const std::string& text) {
    write(LOG_WARNING, text);
}

void fail(const std::string& text) {
    write(LOG_ERR, text);
}

}
