#ifndef CHAT_UTF8_TERMINAL_H
#define CHAT_UTF8_TERMINAL_H

#include <optional>

#include <termios.h>

namespace chat {

// While alive, the terminal edits input lines by whole UTF-8 characters (IUTF8 flag).
// Without it Backspace removes one byte of a two-byte Cyrillic letter and leaves the other
// in the line. Restores the previous settings; does nothing if fd is not a terminal
class Utf8Terminal {
public:
    explicit Utf8Terminal(int fd);
    ~Utf8Terminal();

    Utf8Terminal(const Utf8Terminal&) = delete;
    Utf8Terminal& operator=(const Utf8Terminal&) = delete;

private:
    int fd_;
    std::optional<termios> saved_;
};

}

#endif // CHAT_UTF8_TERMINAL_H
