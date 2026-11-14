#include "logger.hpp"
#include "ipc_support.hpp"
#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <unistd.h>

namespace lab {
Logger::Logger(const std::string& path) : fd_(open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC, 0600)) {
    if (fd_ == -1) systemError("open log " + path);
}
Logger::~Logger() { close(fd_); }
void Logger::write(const std::string& text) const noexcept {
    try {
        const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        const std::string line = std::to_string(stamp) + " pid=" + std::to_string(getpid()) + " " + text + "\n";
        std::size_t offset = 0;
        while (offset < line.size()) {
            const ssize_t count = ::write(fd_, line.data() + offset, line.size() - offset);
            if (count == -1 && errno == EINTR) continue;
            if (count <= 0) { static_cast<void>(::write(STDERR_FILENO, "Cannot write log\n", 17)); break; }
            offset += static_cast<std::size_t>(count);
        }
    } catch (...) { /* Logging must never prevent process/resource cleanup. */ }
}
}
