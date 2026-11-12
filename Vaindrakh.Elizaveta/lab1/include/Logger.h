#pragma once

#include <mutex>
#include <string>

namespace disk_monitor {

class Logger {
public:
    Logger() = default;
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void open();
    void info(const std::string& message);
    void warning(const std::string& message);
    void error(const std::string& message);
    void event(const std::string& message);

private:
    void writeTestRecord(const char* facility, const std::string& message);

    bool opened_ = false;
    std::string test_log_path_;
    std::mutex mutex_;
};

}  // namespace disk_monitor

