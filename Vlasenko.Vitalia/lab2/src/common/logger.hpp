#pragma once

#include <iosfwd>
#include <mutex>
#include <string_view>

namespace lab2::common {

// Один экземпляр используется всеми потоками процесса; поток вывода должен жить дольше него.
class Logger {
public:
    Logger();
    explicit Logger(std::ostream& output);

    void info(std::string_view message);
    void warning(std::string_view message);
    void error(std::string_view message);

private:
    void log(std::string_view level, std::string_view message);

    std::ostream& output_;
    std::mutex mutex_;
};

} // namespace lab2::common
