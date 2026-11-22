#include "chat.hpp"
#include <charconv>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <unistd.h>

namespace chat {
std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}
void validate(const Message& m) {
    if (m.sender < 0 || m.recipient < -1 || m.timestamp <= 0 || m.text.empty() ||
        m.text.size() > max_text || m.text.find_first_of("\r\n\0", 0, 3) != std::string::npos) {
        throw std::runtime_error("Invalid message: text must contain 1..1024 bytes without line breaks/NUL");
    }
}
std::string encode(const Message& m) {
    validate(m);
    return std::to_string(m.sequence) + " " + std::to_string(m.timestamp) + " " +
           std::to_string(m.sender) + " " + std::to_string(m.recipient) + "\n" + m.text;
}
Message decode(const std::string& bytes) {
    const auto separator = bytes.find('\n');
    if (separator == std::string::npos) throw std::runtime_error("Invalid message header");
    std::istringstream header(bytes.substr(0, separator));
    Message m;
    std::string extra;
    if (!(header >> m.sequence >> m.timestamp >> m.sender >> m.recipient) || (header >> extra)) {
        throw std::runtime_error("Invalid message fields");
    }
    m.text = bytes.substr(separator + 1);
    validate(m);
    return m;
}
bool visible_to(const Message& m, int participant) {
    return m.recipient == -1 || m.sender == participant || m.recipient == participant;
}
std::string display(const Message& m) {
    return "MSG " + std::to_string(m.sequence) + " " + std::to_string(m.timestamp) + " " +
           std::to_string(m.sender) + " " + std::to_string(m.recipient) + " " + m.text;
}
void Events::push(std::string text) {
    std::lock_guard lock(mutex_);
    messages_.push_back(std::move(text));
}
std::vector<std::string> Events::take() {
    std::lock_guard lock(mutex_);
    std::vector<std::string> result;
    result.swap(messages_);
    return result;
}
Options options(int argc, char** argv, bool client) {
    Options result;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--headless") result.headless = true;
        else if (arg == "--help" || arg == "-h") result.help = true;
        else if (client && result.host == 0) {
            const auto parsed = std::from_chars(arg.data(), arg.data() + arg.size(), result.host);
            if (parsed.ec != std::errc{} || parsed.ptr != arg.data() + arg.size() ||
                result.host <= 1 || result.host == getpid()) throw std::runtime_error("Invalid host PID");
        } else throw std::runtime_error("Unknown argument: " + arg);
    }
    if (client && !result.help && result.host == 0) throw std::runtime_error("Host PID is required");
    return result;
}
} // namespace chat
