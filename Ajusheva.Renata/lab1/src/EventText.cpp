#include "EventText.hpp"

#include <sys/inotify.h>

namespace diskmond {

namespace {


constexpr std::size_t kActionColumn = 12;

struct SingleEvent {
    std::uint32_t mask;
    Action action;
};

constexpr SingleEvent kSingleEvents[] = {
    {IN_OPEN, Action::Open},
    {IN_ACCESS, Action::Read},
    {IN_MODIFY, Action::Write},
    {IN_CLOSE_NOWRITE, Action::Close},
    {IN_CLOSE_WRITE, Action::CloseWrite},
    {IN_ATTRIB, Action::Attrib},
    {IN_CREATE, Action::Create},
    {IN_DELETE, Action::Delete},
    {IN_DELETE_SELF, Action::DeleteSelf},
    {IN_MOVE_SELF, Action::MoveSelf},
    {IN_UNMOUNT, Action::Unmount},
};

std::string shownPath(const std::string& path, bool isDirectory) {
    std::string shown = escapeForLog(path);
    if (isDirectory && (shown.empty() || shown.back() != '/')) {
        shown += '/';
    }
    return shown;
}

}  

const char* actionName(Action action) {
    switch (action) {
        case Action::Open:
            return "open";
        case Action::Read:
            return "read";
        case Action::Write:
            return "write";
        case Action::Close:
            return "close";
        case Action::CloseWrite:
            return "close-write";
        case Action::Attrib:
            return "attrib";
        case Action::Create:
            return "create";
        case Action::Delete:
            return "delete";
        case Action::Rename:
            return "rename";
        case Action::Move:
            return "move";
        case Action::MoveOut:
            return "move-out";
        case Action::MoveIn:
            return "move-in";
        case Action::DeleteSelf:
            return "delete-self";
        case Action::MoveSelf:
            return "move-self";
        case Action::Unmount:
            return "unmount";
    }
    return "unknown";
}

std::optional<Action> actionForMask(std::uint32_t mask) {
    for (const SingleEvent& event : kSingleEvents) {
        if (mask & event.mask) {
            return event.action;
        }
    }
    return std::nullopt;
}

std::string formatEntry(Action action, bool isDirectory, const std::string& path, const std::string& target) {
    std::string entry = actionName(action);
    entry.resize(kActionColumn, ' ');
    entry += shownPath(path, isDirectory);
    if (!target.empty()) {
        entry += " -> ";
        entry += shownPath(target, isDirectory);
    }
    return entry;
}

std::string escapeForLog(const std::string& path) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string escaped;
    escaped.reserve(path.size());
    for (const char symbol : path) {
        const auto code = static_cast<unsigned char>(symbol);
        if (symbol == '\\') {
            escaped += "\\\\";
        } else if (code < 0x20 || code == 0x7F) {
            escaped += "\\x";
            escaped += kHex[code >> 4];
            escaped += kHex[code & 0x0F];
        } else {
            escaped += symbol;  
        }
    }
    return escaped;
}

}  
