#include "Logger.h"

#include <syslog.h>

namespace {

std::string g_ident = "daemon_lab1";
int g_options = LOG_PID;
bool g_isOpen = false;

int toSyslogPriority(Logger::Level level) {
    switch (level) {
        case Logger::Level::Info:    return LOG_INFO;
        case Logger::Level::Warning: return LOG_WARNING;
        case Logger::Level::Error:   return LOG_ERR;
    }
    return LOG_ERR;
}

}

void Logger::open(const std::string& ident, bool alsoToStderr) {
    g_ident = ident.empty() ? "daemon_lab1" : ident;
    g_options = LOG_PID | (alsoToStderr ? LOG_PERROR : 0);
    reopen();
}

void Logger::reopen() {
    if (g_isOpen) {
        closelog();
    }
    openlog(g_ident.c_str(), g_options, LOG_DAEMON);
    g_isOpen = true;
}

void Logger::close() {
    if (g_isOpen) {
        closelog();
        g_isOpen = false;
    }
}

void Logger::log(Level level, const std::string& message) {
    if (!g_isOpen) {
        reopen();
    }
    syslog(toSyslogPriority(level), "%s", sanitize(message).c_str());
}

void Logger::info(const std::string& message) {
    log(Level::Info, message);
}

void Logger::warning(const std::string& message) {
    log(Level::Warning, message);
}

void Logger::error(const std::string& message) {
    log(Level::Error, message);
}

std::string Logger::sanitize(const std::string& message) {
    static const char kHex[] = "0123456789abcdef";
    std::string out;
    out.reserve(message.size());

    for (char ch : message) {
        const unsigned char c = static_cast<unsigned char>(ch);
        if (c == '\n') {
            out += "\\n";
        } else if (c == '\t') {
            out += "\\t";
        } else if (c < 0x20 || c == 0x7F) {
            out += "\\x";
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        } else {
            out += ch;
        }
    }

    return out;
}