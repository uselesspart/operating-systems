#include "log_util.h"

#include <cstdio>
#include <ctime>
#include <mutex>

std::string Timestamp() {
    std::time_t now = std::time(nullptr);
    std::tm t;
    localtime_r(&now, &t);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &t);
    return buf;
}

void LogLine(const std::string& who, const std::string& msg) {
    static std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    std::printf("[%s] [%s] %s\n", Timestamp().c_str(), who.c_str(), msg.c_str());
    std::fflush(stdout);
}
