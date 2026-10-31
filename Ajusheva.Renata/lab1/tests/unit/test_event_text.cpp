#include "check.hpp"

#include "EventText.hpp"

#include <sys/inotify.h>

using namespace diskmond;

namespace {

std::string actionFor(std::uint32_t mask) {
    const auto action = actionForMask(mask);
    return action ? actionName(*action) : "none";
}

}  

TEST(event_single_masks) {
    CHECK_EQ(actionFor(IN_OPEN), "open");
    CHECK_EQ(actionFor(IN_ACCESS), "read");
    CHECK_EQ(actionFor(IN_MODIFY), "write");
    CHECK_EQ(actionFor(IN_CLOSE_NOWRITE), "close");
    CHECK_EQ(actionFor(IN_CLOSE_WRITE), "close-write");
    CHECK_EQ(actionFor(IN_ATTRIB), "attrib");
    CHECK_EQ(actionFor(IN_CREATE), "create");
    CHECK_EQ(actionFor(IN_DELETE), "delete");
    CHECK_EQ(actionFor(IN_DELETE_SELF), "delete-self");
    CHECK_EQ(actionFor(IN_MOVE_SELF), "move-self");
    CHECK_EQ(actionFor(IN_UNMOUNT), "unmount");
    CHECK_EQ(actionFor(IN_CREATE | IN_ISDIR), "create");
}

TEST(event_moves_are_not_single_events) {
    CHECK_EQ(actionFor(IN_MOVED_FROM), "none");
    CHECK_EQ(actionFor(IN_MOVED_TO), "none");
    CHECK_EQ(actionFor(IN_IGNORED), "none");
    CHECK_EQ(actionFor(IN_Q_OVERFLOW), "none");
}

TEST(event_entry_format) {
    CHECK_EQ(formatEntry(Action::Open, false, "/w/a.txt"), "open        /w/a.txt");
    CHECK_EQ(formatEntry(Action::CloseWrite, false, "/w/a.txt"), "close-write /w/a.txt");

    CHECK_EQ(formatEntry(Action::Create, true, "/w/sub"), "create      /w/sub/");
    CHECK_EQ(formatEntry(Action::Read, true, "/"), "read        /");
    CHECK_EQ(formatEntry(Action::Rename, false, "/w/a", "/w/b"), "rename      /w/a -> /w/b");
    CHECK_EQ(formatEntry(Action::Move, true, "/w/a", "/v/a"), "move        /w/a/ -> /v/a/");
}

TEST(event_columns_are_aligned) {
    
    const Action all[] = {Action::Open,       Action::Read,     Action::Write,   Action::Close,
                          Action::CloseWrite, Action::Attrib,   Action::Create,  Action::Delete,
                          Action::Rename,     Action::Move,     Action::MoveOut, Action::MoveIn,
                          Action::DeleteSelf, Action::MoveSelf, Action::Unmount};
    for (const Action action : all) {
        const std::string entry = formatEntry(action, false, "/p");
        CHECK_EQ(entry.substr(11), " /p");
    }
}

TEST(event_paths_are_escaped) {
    
    CHECK_EQ(escapeForLog("/w/evil\nline"), "/w/evil\\x0Aline");
    CHECK_EQ(escapeForLog("/w/tab\there"), "/w/tab\\x09here");
    CHECK_EQ(escapeForLog("/w/back\\slash"), "/w/back\\\\slash");
    CHECK_EQ(escapeForLog("/w/обычное имя 100% (1).txt"), "/w/обычное имя 100% (1).txt");
    CHECK_EQ(formatEntry(Action::Create, false, "/w/a\nb"), "create      /w/a\\x0Ab");
}
