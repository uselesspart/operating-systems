#pragma once

#include <string>

namespace diskmon {

void initializeLogging(bool foreground);
void logDaemon(int priority, const std::string& message);
void logFilesystemEvent(const std::string& message);
void closeLogging();
std::string currentTimestamp();
std::string sanitizeLogField(const std::string& value);
}  

