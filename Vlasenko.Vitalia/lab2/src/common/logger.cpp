#include "common/logger.hpp"

#include <iostream>

namespace lab2::common {

Logger::Logger() : Logger(std::clog) {
}

Logger::Logger(std::ostream& output) : output_(output) {
}

void Logger::info(std::string_view message) {
    log("INFO", message);
}

void Logger::warning(std::string_view message) {
    log("WARNING", message);
}

void Logger::error(std::string_view message) {
    log("ERROR", message);
}

void Logger::log(std::string_view level, std::string_view message) {
    const std::lock_guard<std::mutex> lock(mutex_);
    output_ << '[' << level << "] " << message << '\n';
    output_.flush();
}

} // namespace lab2::common
