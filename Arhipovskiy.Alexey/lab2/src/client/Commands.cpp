#include "client/Commands.h"

#include "chat/Text.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <utility>

namespace chat {

namespace {

Command invalid(std::string error)
{
    return {Command::Kind::Invalid, kEveryone, std::move(error)};
}

// "<id> <text>" after a private-message prefix
Command parsePrivate(const std::string& rest)
{
    const auto space = rest.find(' ');
    const std::string number = rest.substr(0, space);
    const std::string text =
        space == std::string::npos ? std::string() : text::sanitizeText(rest.substr(space + 1));

    char* end = nullptr;
    errno = 0;
    const long id = std::strtol(number.c_str(), &end, 10);
    if (number.empty() || *end != '\0' || errno != 0 || id < 0 || id > INT_MAX) {
        return invalid("Укажите номер участника: /to <номер> <текст>");
    }
    if (text.empty()) {
        return invalid("Пустое сообщение не отправлено");
    }
    return {Command::Kind::Private, static_cast<int>(id), text};
}

}

Command parseCommand(const std::string& line)
{
    const std::string input = text::sanitizeText(line);
    if (input.empty()) {
        return {};
    }
    if (input == "/quit" || input == "/exit") {
        return {Command::Kind::Quit};
    }
    if (input == "/who") {
        return {Command::Kind::Who};
    }
    if (input == "/help") {
        return {Command::Kind::Help};
    }
    if (input == "/to" || input.rfind("/to ", 0) == 0) {
        return parsePrivate(text::sanitizeText(input.substr(3)));
    }
    if (input.front() == '@') {
        return parsePrivate(input.substr(1));
    }
    if (input.front() == '/') {
        return invalid("Неизвестная команда, список команд: /help");
    }
    return {Command::Kind::Say, kEveryone, input};
}

}
