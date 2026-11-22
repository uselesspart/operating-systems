#include <cstdio>
#include <string>

#include "check.h"
#include "message.h"

namespace {

WireMessage text(Party sender, Party recipient)
{
    return *makeText(sender, recipient, "hi");
}

void visibility()
{
    CHECK(isVisibleTo(text(Party::Host, Party::Everyone), Party::Client));
    CHECK(isVisibleTo(text(Party::Client, Party::Everyone), Party::Host));

    CHECK(isVisibleTo(text(Party::Host, Party::Client), Party::Host));
    CHECK(isVisibleTo(text(Party::Host, Party::Client), Party::Client));
    CHECK(isVisibleTo(text(Party::Client, Party::Host), Party::Host));
    CHECK(isVisibleTo(text(Party::Client, Party::Host), Party::Client));

    CHECK(isVisibleTo(text(Party::Client, Party::Client), Party::Client));
    CHECK(!isVisibleTo(text(Party::Client, Party::Client), Party::Host));
    CHECK(isVisibleTo(text(Party::Host, Party::Host), Party::Host));
    CHECK(!isVisibleTo(text(Party::Host, Party::Host), Party::Client));
}

void textLimits()
{
    const std::string longest(kMaxTextBytes, 'a');
    const auto accepted = makeText(Party::Host, Party::Everyone, longest);
    CHECK(accepted && textOf(*accepted) == longest);
    CHECK(!makeText(Party::Host, Party::Everyone, longest + "a"));

    const std::string utf8 = "Привет, мир";
    const auto message = makeText(Party::Client, Party::Host, utf8);
    CHECK(message && textOf(*message) == utf8);
    CHECK(message && message->sender == Party::Client && message->recipient == Party::Host);
    CHECK(message && message->sentAtUs > 0);
}

void frameValidation()
{
    WireMessage message = makeBye(Party::Host);
    CHECK(isValid(message));
    CHECK(message.kind == Kind::Bye);

    message.kind = static_cast<Kind>(9);
    CHECK(!isValid(message));

    message = text(Party::Host, Party::Everyone);
    CHECK(isValid(message));
    message.length = kMaxTextBytes + 1;
    CHECK(!isValid(message));

    message = text(Party::Host, Party::Everyone);
    message.sender = Party::Everyone;
    CHECK(!isValid(message));
}

void peers()
{
    CHECK(peerOf(Party::Host) == Party::Client);
    CHECK(peerOf(Party::Client) == Party::Host);
}

}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    RUN(visibility);
    RUN(textLimits);
    RUN(frameValidation);
    RUN(peers);
    return g_failures == 0 ? 0 : 1;
}
