#ifndef CHAT_LOG_H
#define CHAT_LOG_H

#include <string>

// Thin wrapper over syslog; the host mirrors records to stderr
namespace chat::log {

void open(const char* ident, bool mirrorToStderr);
void close();

void info(const std::string& message);
void warning(const std::string& message);
void error(const std::string& message);

}

#endif // CHAT_LOG_H
