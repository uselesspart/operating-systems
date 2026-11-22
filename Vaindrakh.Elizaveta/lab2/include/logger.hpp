#pragma once
#include <string>

namespace lab {
class Logger {
public:
    explicit Logger(const std::string& path);
    ~Logger();
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    void write(const std::string& text) const noexcept;
private:
    int fd_;
};
}
