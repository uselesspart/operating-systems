#include "Logger.h"

#include <syslog.h>

#include <cstdlib>
#include <fstream>

#include <unistd.h>

namespace disk_monitor {

Logger::~Logger() {
    if (opened_) {
        closelog();
    }
}

void Logger::open() {
    openlog("disk_monitor", LOG_PID | LOG_NDELAY, LOG_DAEMON);
    if (const char* path = std::getenv("DISK_MONITOR_TEST_LOG"); path != nullptr) {
        test_log_path_ = path;
    }
    opened_ = true;
}

void Logger::writeTestRecord(const char* facility, const std::string& message) {
    if (test_log_path_.empty()) {
        return;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    std::ofstream output(test_log_path_, std::ios::app);
    if (output) {
        output << facility << " pid=" << getpid() << ' ' << message << '\n';
    }
}

void Logger::info(const std::string& message) {
    syslog(LOG_INFO, "%s", message.c_str());
    writeTestRecord("daemon", message);
}

void Logger::warning(const std::string& message) {
    syslog(LOG_WARNING, "%s", message.c_str());
    writeTestRecord("daemon", message);
}

void Logger::error(const std::string& message) {
    syslog(LOG_ERR, "%s", message.c_str());
    writeTestRecord("daemon", message);
}

void Logger::event(const std::string& message) {
    syslog(LOG_INFO | LOG_LOCAL0, "%s", message.c_str());
    writeTestRecord("local0", message);
}

}  // namespace disk_monitor

