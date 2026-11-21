#include "check.hpp"

#include "client/Command.hpp"

using namespace chatlab;
using Kind = Command::Kind;

TEST(command_plain_text_is_public) {
    const Command command = parseCommand("  всем привет ");
    CHECK(command.kind == Kind::Public);
    CHECK_EQ(command.to, kEveryone);
    CHECK_EQ(command.text, std::string("всем привет"));
}

TEST(command_private_forms) {
    const Command slash = parseCommand("/to 3 секрет тут");
    CHECK(slash.kind == Kind::Private);
    CHECK_EQ(slash.to, 3);
    CHECK_EQ(slash.text, std::string("секрет тут"));

    const Command at = parseCommand("@0 хосту");
    CHECK(at.kind == Kind::Private);
    CHECK_EQ(at.to, 0);
    CHECK_EQ(at.text, std::string("хосту"));
}

TEST(command_private_errors) {
    CHECK(parseCommand("@ текст").kind == Kind::Wrong);
    CHECK(parseCommand("/to x текст").kind == Kind::Wrong);
    CHECK(parseCommand("/to 5").kind == Kind::Wrong);
    CHECK(parseCommand("@12345678 a").kind == Kind::Wrong);
}

TEST(command_service_words) {
    CHECK(parseCommand("/who").kind == Kind::Who);
    CHECK(parseCommand("/help").kind == Kind::Help);
    CHECK(parseCommand("/quit").kind == Kind::Quit);
    CHECK(parseCommand("/exit").kind == Kind::Quit);
    CHECK(parseCommand("/nope").kind == Kind::Wrong);
    CHECK(parseCommand("   ").kind == Kind::Nothing);
    CHECK(parseCommand("\r").kind == Kind::Nothing);
}
