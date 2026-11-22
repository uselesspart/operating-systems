#include "util/Log.h"

#include <syslog.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>

namespace wolf::log {

namespace {

std::mutex g_mutex;
std::string g_ident = "wolf";

std::string timestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const std::time_t t = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d:%02d.%03d", tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(ms));
    return buf;
}

void write(int priority, const char* level, const std::string& message) {
    std::lock_guard<std::mutex> lock(g_mutex);
    const std::string line =
        "[" + timestamp() + "] " + g_ident + " " + level + ": " + message + "\n";
    const char* data = line.data();
    std::size_t left = line.size();
    while (left > 0) {
        const ssize_t written = ::write(STDERR_FILENO, data, left);
        if (written > 0) {
            data += written;
            left -= static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        break;
    }
    syslog(priority, "%s", message.c_str());
}

}

void init(const std::string& ident) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_ident = ident;
    openlog(g_ident.c_str(), LOG_PID, LOG_USER);
}

void info(const std::string& message) { write(LOG_INFO, "INFO ", message); }
void warn(const std::string& message) { write(LOG_WARNING, "WARN ", message); }
void error(const std::string& message) { write(LOG_ERR, "ERROR", message); }

}
