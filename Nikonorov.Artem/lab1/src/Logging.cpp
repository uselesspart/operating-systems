#include "disk_monitor/Logging.hpp"

#include <ctime>
#include <iomanip>
#include <sstream>
#include <syslog.h>

namespace diskmon {
namespace {

bool loggingInitialized = false;

}  // namespace

void initializeLogging(bool foreground) {
    int options = LOG_PID | LOG_NDELAY;
    if (foreground) {
        options |= LOG_CONS;
    }
    ::openlog("disk-monitor", options, LOG_DAEMON);
    loggingInitialized = true;
}

void logDaemon(int priority, const std::string& message) {
    if (!loggingInitialized) {
        initializeLogging(false);
    }
    ::syslog(LOG_DAEMON | priority, "%s", message.c_str());
}

void logFilesystemEvent(const std::string& message) {
    if (!loggingInitialized) {
        initializeLogging(false);
    }
    ::syslog(LOG_LOCAL0 | LOG_INFO, "%s", message.c_str());
}

void closeLogging() {
    if (loggingInitialized) {
        ::closelog();
        loggingInitialized = false;
    }
}

std::string currentTimestamp() {
    const std::time_t now = std::time(nullptr);
    std::tm localTime{};
    if (::localtime_r(&now, &localTime) == nullptr) {
        return "unknown-time";
    }
    char buffer[40]{};
    if (::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S%z", &localTime) == 0U) {
        return "unknown-time";
    }
    return buffer;
}

std::string sanitizeLogField(const std::string& value) {
    std::ostringstream result;
    for (const unsigned char character : value) {
        if (character == '\\') {
            result << "\\\\";
        } else if (character == ' ') {
            result << "\\x20";
        } else if (character == '=') {
            result << "\\x3d";
        } else if (character == '\n') {
            result << "\\n";
        } else if (character == '\r') {
            result << "\\r";
        } else if (character == '\t') {
            result << "\\t";
        } else if (character < 0x20U || character == 0x7fU) {
            result << "\\x" << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<unsigned int>(character) << std::dec << std::setfill(' ');
        } else {
            result << static_cast<char>(character);
        }
    }
    return result.str();
}

}  // namespace diskmon
