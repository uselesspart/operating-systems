#include "Logger.h"

#include <syslog.h>

namespace diskmon::logger {

namespace {

constexpr const char* kIdent = "disk_monitor";
constexpr int kServiceFacility = LOG_DAEMON;
constexpr int kEventFacility = LOG_LOCAL0;

void emit(int priority, const std::string& message)
{
    syslog(priority, "%s", message.c_str());
}

}

void open(Output output)
{
    const int options = LOG_PID | (output == Output::SyslogAndStderr ? LOG_PERROR : 0);
    openlog(kIdent, options, kServiceFacility);
}

void close()
{
    closelog();
}

void info(const std::string& message)
{
    emit(kServiceFacility | LOG_INFO, message);
}

void warning(const std::string& message)
{
    emit(kServiceFacility | LOG_WARNING, message);
}

void error(const std::string& message)
{
    emit(kServiceFacility | LOG_ERR, message);
}

void event(const std::string& message)
{
    emit(kEventFacility | LOG_INFO, message);
}

}
