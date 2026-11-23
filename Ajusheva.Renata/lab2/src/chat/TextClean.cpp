#include "chat/TextClean.hpp"

#include "chat/Limits.hpp"

namespace chatlab::textclean {

namespace {

constexpr unsigned char kEscape = 0x1B;

std::size_t sequenceLength(unsigned char lead) {
    if (lead < 0x80) {
        return 1;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        return 2;
    }
    if (lead >= 0xE0 && lead <= 0xEF) {
        return 3;
    }
    if (lead >= 0xF0 && lead <= 0xF4) {
        return 4;
    }
    return 0;
}

bool isContinuation(unsigned char byte) {
    return (byte & 0xC0) == 0x80;
}

bool validSequence(const std::string& text, std::size_t at, std::size_t length) {
    if (at + length > text.size()) {
        return false;
    }
    for (std::size_t index = 1; index < length; ++index) {
        if (!isContinuation(static_cast<unsigned char>(text[at + index]))) {
            return false;
        }
    }
    const auto lead = static_cast<unsigned char>(text[at]);
    const auto next = length > 1 ? static_cast<unsigned char>(text[at + 1]) : 0;
    if (lead == 0xE0 && next < 0xA0) {
        return false;
    }
    if (lead == 0xED && next > 0x9F) {
        return false;
    }
    if (lead == 0xF0 && next < 0x90) {
        return false;
    }
    if (lead == 0xF4 && next > 0x8F) {
        return false;
    }
    return true;
}

std::size_t skipEscape(const std::string& text, std::size_t at) {
    std::size_t index = at + 1;
    if (index < text.size() && text[index] == '[') {
        ++index;
        while (index < text.size()) {
            const auto byte = static_cast<unsigned char>(text[index++]);
            if (byte >= 0x40 && byte <= 0x7E) {
                break;
            }
        }
        return index;
    }
    return index < text.size() ? index + 1 : index;
}

bool isC1Control(const std::string& text, std::size_t at) {
    return static_cast<unsigned char>(text[at]) == 0xC2 &&
           static_cast<unsigned char>(text[at + 1]) < 0xA0;
}

std::string trim(const std::string& text) {
    const std::size_t first = text.find_first_not_of(' ');
    if (first == std::string::npos) {
        return {};
    }
    const std::size_t last = text.find_last_not_of(' ');
    return text.substr(first, last - first + 1);
}

}

std::string cutUtf8(const std::string& text, std::size_t maxBytes) {
    if (text.size() <= maxBytes) {
        return text;
    }
    std::size_t end = maxBytes;
    while (end > 0 && isContinuation(static_cast<unsigned char>(text[end]))) {
        --end;
    }
    return text.substr(0, end);
}

std::string cleanText(const std::string& raw) {
    std::string result;
    result.reserve(raw.size());
    std::size_t at = 0;
    while (at < raw.size()) {
        const auto byte = static_cast<unsigned char>(raw[at]);
        if (byte == kEscape) {
            at = skipEscape(raw, at);
            continue;
        }
        const std::size_t length = sequenceLength(byte);
        if (length == 0 || !validSequence(raw, at, length)) {
            ++at;
            continue;
        }
        if (length == 1 && (byte < 0x20 || byte == 0x7F)) {
            result.push_back(' ');
        } else if (length == 2 && isC1Control(raw, at)) {
            result.push_back(' ');
        } else {
            result.append(raw, at, length);
        }
        at += length;
    }
    return cutUtf8(trim(result), kTextLimit);
}

std::string cleanName(const std::string& raw) {
    return trim(cutUtf8(cleanText(raw), kNameLimit));
}

}
