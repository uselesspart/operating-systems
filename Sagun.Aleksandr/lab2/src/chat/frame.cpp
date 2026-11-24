#include "frame.hpp"

#include <cstddef>
#include <stdexcept>

namespace chat {
namespace {
constexpr std::size_t headerSize = 2;
constexpr std::size_t maxTextSize = 4096;

void checkSize(std::size_t size)
{
    if (size == 0 || size > maxTextSize) {
        throw std::length_error("Message length must be 1..4096 bytes");
    }
}
} // namespace

std::string encodeFrame(std::string_view text)
{
    checkSize(text.size());
    std::string frame;
    frame.reserve(headerSize + text.size());
    frame.push_back(static_cast<char>(text.size() >> 8));
    frame.push_back(static_cast<char>(text.size() & 0xff));
    frame.append(text);
    return frame;
}

std::optional<std::string> takeFrame(std::string& buffer)
{
    if (buffer.size() < headerSize) {
        return std::nullopt;
    }

    const auto high = static_cast<unsigned char>(buffer[0]);
    const auto low = static_cast<unsigned char>(buffer[1]);
    const std::size_t size = (high << 8) | low;
    checkSize(size);

    if (buffer.size() < headerSize + size) {
        return std::nullopt;
    }

    std::string text = buffer.substr(headerSize, size);
    buffer.erase(0, headerSize + size);
    return text;
}

} // namespace chat
