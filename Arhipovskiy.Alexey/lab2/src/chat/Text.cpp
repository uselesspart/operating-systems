#include "chat/Text.h"

#include "chat/Limits.h"

namespace chat::text {

namespace {

constexpr unsigned char kEscape = 0x1b;

unsigned char byteAt(const std::string& text, std::size_t pos)
{
    return pos < text.size() ? static_cast<unsigned char>(text[pos]) : 0;
}

bool isContinuation(unsigned char byte)
{
    return (byte & 0xC0) == 0x80;
}

// Length of the well-formed UTF-8 character at pos, 0 if the bytes there are not one
// (stray continuation byte, cut or overlong sequence, surrogate, code point above U+10FFFF)
std::size_t utf8Length(const std::string& text, std::size_t pos)
{
    const unsigned char lead = byteAt(text, pos);
    std::size_t length = 0;
    unsigned char secondMin = 0x80;
    unsigned char secondMax = 0xBF;
    if (lead < 0x80) {
        return 1;
    }
    if (lead >= 0xC2 && lead <= 0xDF) {
        length = 2;
    } else if (lead >= 0xE0 && lead <= 0xEF) {
        length = 3;
        secondMin = lead == 0xE0 ? 0xA0 : 0x80;
        secondMax = lead == 0xED ? 0x9F : 0xBF;
    } else if (lead >= 0xF0 && lead <= 0xF4) {
        length = 4;
        secondMin = lead == 0xF0 ? 0x90 : 0x80;
        secondMax = lead == 0xF4 ? 0x8F : 0xBF;
    } else {
        return 0;
    }

    const unsigned char second = byteAt(text, pos + 1);
    if (second < secondMin || second > secondMax) {
        return 0;
    }
    for (std::size_t i = 2; i < length; ++i) {
        if (!isContinuation(byteAt(text, pos + i))) {
            return 0;
        }
    }
    return length;
}

// ESC [ <parameters> <final byte> (arrows, colours), ESC O <key> (arrows in application mode)
// or a lone ESC
std::size_t escapeLength(const std::string& text, std::size_t pos)
{
    const unsigned char kind = byteAt(text, pos + 1);
    if (kind == 'O' && pos + 2 < text.size()) {
        return 3;
    }
    if (kind != '[') {
        return 1;
    }
    std::size_t end = pos + 2;
    while (byteAt(text, end) >= 0x20 && byteAt(text, end) <= 0x3F) {
        ++end;
    }
    if (byteAt(text, end) >= 0x40 && byteAt(text, end) <= 0x7E) {
        ++end;
    }
    return end - pos;
}

// C0 controls, DEL and C1 controls (U+0080..U+009F, which some terminals also obey)
bool isControl(const std::string& text, std::size_t pos, std::size_t length)
{
    const unsigned char lead = byteAt(text, pos);
    if (length == 1) {
        return lead < 0x20 || lead == 0x7F;
    }
    return length == 2 && lead == 0xC2 && byteAt(text, pos + 1) <= 0x9F;
}

}

std::string sanitizeText(const std::string& text)
{
    std::string result;
    result.reserve(text.size());
    std::size_t pos = 0;
    while (pos < text.size()) {
        if (byteAt(text, pos) == kEscape) {
            pos += escapeLength(text, pos);
            continue;
        }
        const std::size_t length = utf8Length(text, pos);
        if (length == 0) {
            ++pos;
            continue;
        }
        if (isControl(text, pos, length)) {
            result += ' ';
        } else {
            result.append(text, pos, length);
        }
        pos += length;
    }

    const auto begin = result.find_first_not_of(' ');
    if (begin == std::string::npos) {
        return {};
    }
    const auto end = result.find_last_not_of(' ');
    return result.substr(begin, end - begin + 1);
}

std::string sanitizeName(const std::string& name)
{
    return sanitizeText(truncateUtf8(sanitizeText(name), kMaxNameBytes));
}

std::string truncateUtf8(const std::string& text, std::size_t maxBytes)
{
    if (text.size() <= maxBytes) {
        return text;
    }
    std::size_t cut = maxBytes;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0) == 0x80) {
        --cut;
    }
    return text.substr(0, cut);
}

std::string formatMessage(const ChatMessage& message, int selfId)
{
    const std::string from = message.from == selfId ? "Вы" : message.fromName;
    std::string route = from;
    if (message.isPrivate()) {
        const std::string to = message.to == selfId ? "вам" : message.toName;
        route += " → " + to + " (лично)";
    }
    return "[" + formatTime(message.sentAtMs) + "] " + route + ": " + message.text;
}

}
