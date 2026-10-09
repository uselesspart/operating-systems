#ifndef CHAT_COMMANDS_H
#define CHAT_COMMANDS_H

#include "chat/Message.h"

#include <string>

namespace chat {

// One line typed in the console client
struct Command {
    enum class Kind { Empty, Say, Private, Who, Help, Quit, Invalid };

    Kind kind = Kind::Empty;
    int to = kEveryone;
    std::string text;
};

// "text" - to everyone; "/to <id> text" or "@<id> text" - private; "/who", "/help", "/quit".
// For Kind::Invalid the text is an error message
Command parseCommand(const std::string& line);

}

#endif // CHAT_COMMANDS_H
