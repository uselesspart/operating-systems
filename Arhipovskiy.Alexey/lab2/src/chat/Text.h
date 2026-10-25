#ifndef CHAT_TEXT_H
#define CHAT_TEXT_H

#include "chat/Message.h"

#include <cstddef>
#include <string>

namespace chat::text {

// Makes text safe and readable for every participant: drops terminal escape sequences
// (e.g. arrow keys typed into a line) and broken UTF-8 (e.g. half a letter left by Backspace),
// turns other control characters into spaces and trims surrounding whitespace
std::string sanitizeText(const std::string& text);

// Like sanitizeText, then cut to kMaxNameBytes without splitting a UTF-8 character
std::string sanitizeName(const std::string& name);

std::string truncateUtf8(const std::string& text, std::size_t maxBytes);

// "[12:00:01] Алиса → вам (лично): текст", phrased for the participant selfId
std::string formatMessage(const ChatMessage& message, int selfId);

}

#endif // CHAT_TEXT_H
