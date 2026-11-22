#include "arguments.h"

#include <charconv>
#include <stdexcept>

pid_t read_pid(const std::string& text) {
    pid_t pid = 0;
    auto result = std::from_chars(text.data(), text.data() + text.size(), pid);
    if (result.ec != std::errc() || result.ptr != text.data() + text.size() || pid <= 0) {
        throw std::runtime_error("Invalid PID");
    }
    return pid;
}
