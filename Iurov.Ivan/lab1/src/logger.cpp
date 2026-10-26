#include "treecleaner/logger.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/stat.h>
#include <unistd.h>

namespace treecleaner {
namespace {

void writeAll(int fd, const std::string& text) {
    const char* data = text.data();
    std::size_t remaining = text.size();
    while (remaining > 0) {
        const ssize_t written = ::write(fd, data, remaining);
        if (written < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;
        }
        if (written == 0) {
            return;
        }
        data += written;
        remaining -= static_cast<std::size_t>(written);
    }
}

} // namespace

Logger::Logger(const std::string& path) : fd_(STDERR_FILENO), ownsFd_(false) {
    if (path.empty()) {
        return;
    }

    fd_ = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC | O_NOFOLLOW, S_IRUSR | S_IWUSR);
    if (fd_ < 0) {
        throw std::runtime_error("не удалось открыть журнал '" + path + "': " + std::strerror(errno));
    }
    ownsFd_ = true;
}

Logger::~Logger() {
    if (ownsFd_ && fd_ >= 0) {
        ::close(fd_);
    }
}

void Logger::write(const char* level, const std::string& message) const {
    const std::time_t now = std::time(nullptr);
    std::tm localTime{};
    if (::localtime_r(&now, &localTime) == nullptr) {
        writeAll(fd_, std::string("[unknown time] [") + level + "] " + message + "\n");
        return;
    }

    std::ostringstream output;
    output << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S") << " [" << level << "] " << message << '\n';
    writeAll(fd_, output.str());
}

void Logger::info(const std::string& message) const { write("INFO", message); }
void Logger::warning(const std::string& message) const { write("WARN", message); }
void Logger::error(const std::string& message) const { write("ERROR", message); }

} // namespace treecleaner
