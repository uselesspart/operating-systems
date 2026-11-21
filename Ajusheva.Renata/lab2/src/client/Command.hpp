#pragma once

#include "chat/Post.hpp"

#include <string>

namespace chatlab {

struct Command {
    enum class Kind { Nothing, Public, Private, Who, Help, Quit, Wrong };

    Kind kind = Kind::Nothing;
    int to = kEveryone;
    std::string text;
};

Command parseCommand(const std::string& line);

}
