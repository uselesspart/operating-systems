#include "Journal.hpp"

#include <syslog.h>

namespace diskmond::journal {

namespace {

constexpr const char* kIdent = "diskmond";
constexpr int kStatusFacility = LOG_DAEMON;
constexpr int kFileEventFacility = LOG_LOCAL0;

}  

void open(Echo echo) {
    const int flags = LOG_PID | (echo == Echo::Stderr ? LOG_PERROR : 0);
    ::openlog(kIdent, flags, kStatusFacility);
}

void close() {
    ::closelog();
}


void status(int severity, const std::string& text) {
    ::syslog(kStatusFacility | severity, "%s", text.c_str());
}

void fileEvent(const std::string& text) {
    ::syslog(kFileEventFacility | LOG_INFO, "%s", text.c_str());
}

}  
