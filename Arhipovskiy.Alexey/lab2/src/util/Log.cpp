#include "util/Log.h"

#include <syslog.h>

namespace chat::log {

namespace {

void emit(int priority, const std::string& message)
{
    syslog(priority, "%s", message.c_str());
}

}

void open(const char* ident, bool mirrorToStderr)
{
    openlog(ident, LOG_PID | (mirrorToStderr ? LOG_PERROR : 0), LOG_USER);
}

void close()
{
    closelog();
}

void info(const std::string& message)
{
    emit(LOG_INFO, message);
}

void warning(const std::string& message)
{
    emit(LOG_WARNING, message);
}

void error(const std::string& message)
{
    emit(LOG_ERR, message);
}

}
