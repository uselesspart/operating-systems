#include "client/LineReader.h"

#include "ipc/FdIo.h"

#include <cerrno>

#include <unistd.h>

namespace chat {

LineReader::Status LineReader::readLine(std::string& line, std::chrono::milliseconds timeout)
{
    if (takeLine(line)) {
        return Status::Line;
    }
    if (end_) {
        return Status::End;
    }
    if (!fdio::waitReadable(fd_, timeout)) {
        return Status::Timeout;
    }

    char chunk[4096];
    const ssize_t n = ::read(fd_, chunk, sizeof(chunk));
    if (n > 0) {
        buffer_.append(chunk, static_cast<std::size_t>(n));
    } else if (n == 0 || errno != EINTR) {
        end_ = true;
    }
    if (takeLine(line)) {
        return Status::Line;
    }
    return end_ ? Status::End : Status::Timeout;
}

bool LineReader::takeLine(std::string& line)
{
    const auto newline = buffer_.find('\n');
    if (newline != std::string::npos) {
        line = buffer_.substr(0, newline);
        buffer_.erase(0, newline + 1);
        return true;
    }
    if (end_ && !buffer_.empty()) {
        line = std::move(buffer_);
        buffer_.clear();
        return true;
    }
    return false;
}

}
