#ifndef DISKMON_LOGGER_H
#define DISKMON_LOGGER_H

#include <string>

// Service messages use LOG_DAEMON, file events use LOG_LOCAL0
namespace diskmon::logger {

enum class Output { Syslog, SyslogAndStderr };

void open(Output output);
void close();

void info(const std::string& message);
void warning(const std::string& message);
void error(const std::string& message);
void event(const std::string& message);

}

#endif // DISKMON_LOGGER_H
