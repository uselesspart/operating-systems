#pragma once

#include <string>

// Тонкая обёртка над openlog / syslog / closelog.
// Все функции статические: объект Logger создавать не нужно.
class Logger {
public:
    enum class Level { Info, Warning, Error };

    Logger() = delete;

    static void open(const std::string& ident, bool alsoToStderr = false);

    static void reopen();

    static void close();

    static void log(Level level, const std::string& message);
    static void info(const std::string& message);
    static void warning(const std::string& message);
    static void error(const std::string& message);

    static std::string sanitize(const std::string& message);
};