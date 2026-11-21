#include "client/LineInput.hpp"

#include <cerrno>

#include <poll.h>
#include <unistd.h>

namespace chatlab {

namespace {

constexpr std::size_t kChunk = 4096;
constexpr std::size_t kLineLimit = 16 * 1024;

}

LineInput::LineInput(int fd) : fd_(fd) {}

void LineInput::splitLines(std::vector<std::string>& lines) {
    std::size_t start = 0;
    for (auto end = pending_.find('\n'); end != std::string::npos; end = pending_.find('\n', start)) {
        lines.push_back(pending_.substr(start, end - start));
        start = end + 1;
    }
    pending_.erase(0, start);
    if (pending_.size() > kLineLimit) {
        lines.push_back(pending_);
        pending_.clear();
    }
}

std::vector<std::string> LineInput::poll(std::chrono::milliseconds timeout) {
    std::vector<std::string> lines;
    if (closed_) {
        return lines;
    }
    pollfd watched{fd_, POLLIN, 0};
    const int ready = ::poll(&watched, 1, static_cast<int>(timeout.count()));
    if (ready <= 0) {
        return lines;
    }
    char chunk[kChunk];
    const ssize_t got = ::read(fd_, chunk, sizeof(chunk));
    if (got < 0 && (errno == EINTR || errno == EAGAIN)) {
        return lines;
    }
    if (got <= 0) {
        closed_ = true;
        if (!pending_.empty()) {
            lines.push_back(pending_);
            pending_.clear();
        }
        return lines;
    }
    pending_.append(chunk, static_cast<std::size_t>(got));
    splitLines(lines);
    return lines;
}

}
