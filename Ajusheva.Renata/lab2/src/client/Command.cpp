#include "client/Command.hpp"

#include <cctype>
#include <cstdlib>

namespace chatlab {

namespace {

std::string stripSpaces(const std::string& text) {
    const auto first = text.find_first_not_of(" \t\r");
    if (first == std::string::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r");
    return text.substr(first, last - first + 1);
}

Command wrong(const std::string& why) {
    return {Command::Kind::Wrong, kEveryone, why};
}

Command personal(const std::string& rest) {
    std::size_t digits = 0;
    while (digits < rest.size() && std::isdigit(static_cast<unsigned char>(rest[digits]))) {
        ++digits;
    }
    if (digits == 0 || digits > 6) {
        return wrong("укажите номер участника: /to N текст или @N текст");
    }
    const int to = std::atoi(rest.substr(0, digits).c_str());
    const std::string text = stripSpaces(rest.substr(digits));
    if (text.empty()) {
        return wrong("пустое личное сообщение");
    }
    return {Command::Kind::Private, to, text};
}

}

Command parseCommand(const std::string& line) {
    const std::string text = stripSpaces(line);
    if (text.empty()) {
        return {};
    }
    if (text[0] == '@') {
        return personal(text.substr(1));
    }
    if (text[0] != '/') {
        return {Command::Kind::Public, kEveryone, text};
    }
    const auto space = text.find(' ');
    const std::string word = text.substr(0, space);
    const std::string rest = space == std::string::npos ? "" : stripSpaces(text.substr(space + 1));
    if (word == "/to") {
        return personal(rest);
    }
    if (word == "/who") {
        return {Command::Kind::Who};
    }
    if (word == "/help") {
        return {Command::Kind::Help};
    }
    if (word == "/quit" || word == "/exit") {
        return {Command::Kind::Quit};
    }
    return wrong("неизвестная команда " + word + ", список команд: /help");
}

}
