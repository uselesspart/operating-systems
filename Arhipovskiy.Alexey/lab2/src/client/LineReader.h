#ifndef CHAT_LINE_READER_H
#define CHAT_LINE_READER_H

#include <chrono>
#include <string>

namespace chat {

// Reads lines from a descriptor with a timeout, so the reading loop can notice a lost connection
class LineReader {
public:
    enum class Status { Line, Timeout, End };

    explicit LineReader(int fd) : fd_(fd) {}

    Status readLine(std::string& line, std::chrono::milliseconds timeout);

private:
    bool takeLine(std::string& line);

    int fd_;
    std::string buffer_;
    bool end_ = false;
};

}

#endif // CHAT_LINE_READER_H
