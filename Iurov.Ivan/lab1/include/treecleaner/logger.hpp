#pragma once

#include <string>

namespace treecleaner {

class Logger {
public:
    // With an empty path, writes to stderr. Otherwise opens an append-only log
    // file and fails with std::runtime_error if it cannot be opened safely.
    explicit Logger(const std::string& path = {});
    ~Logger();

    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

    void info(const std::string& message) const;
    void warning(const std::string& message) const;
    void error(const std::string& message) const;

private:
    void write(const char* level, const std::string& message) const;
    int fd_;
    bool ownsFd_;
};

} // namespace treecleaner
