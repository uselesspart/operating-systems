#include "client/Utf8Terminal.h"

namespace chat {

Utf8Terminal::Utf8Terminal(int fd) : fd_(fd)
{
    termios settings{};
    if (::tcgetattr(fd_, &settings) != 0 || (settings.c_iflag & IUTF8) != 0) {
        return;
    }
    const termios original = settings;
    settings.c_iflag |= IUTF8;
    if (::tcsetattr(fd_, TCSANOW, &settings) == 0) {
        saved_ = original;
    }
}

Utf8Terminal::~Utf8Terminal()
{
    if (saved_) {
        ::tcsetattr(fd_, TCSANOW, &*saved_);
    }
}

}
