#include "chat/frame.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

void check(bool ok, const char* message)
{
    if (!ok) {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

int main()
{
    const std::string first = chat::encodeFrame("Привет");
    std::string buffer = first.substr(0, 1);
    check(!chat::takeFrame(buffer), "Incomplete header accepted");
    buffer += first.substr(1, 3);
    check(!chat::takeFrame(buffer), "Incomplete text accepted");
    buffer += first.substr(4);
    buffer += chat::encodeFrame("OK");
    check(chat::takeFrame(buffer) == "Привет", "First message lost");
    check(chat::takeFrame(buffer) == "OK", "Second message lost");
    check(buffer.empty(), "Unexpected trailing bytes");

    const std::string longest(4096, 'x');
    buffer = chat::encodeFrame(longest);
    check(chat::takeFrame(buffer) == longest, "Maximum length rejected");
    buffer.assign(2, static_cast<char>(0xff));
    try {
        chat::takeFrame(buffer);
        check(false, "Invalid length accepted");
    } catch (const std::length_error&) {
        // Expected: the incoming frame exceeds our limit.
    }
    std::cout << "frame_check: OK\n";
}
