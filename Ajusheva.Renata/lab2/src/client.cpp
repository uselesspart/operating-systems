#include "chat/Limits.hpp"
#include "client/Command.hpp"
#include "client/Console.hpp"
#include "client/GuestSession.hpp"
#include "client/LineInput.hpp"
#include "conn/conn.hpp"
#include "link/Signals.hpp"
#include "util/Journal.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <unistd.h>

namespace {

using Steady = std::chrono::steady_clock;

std::optional<pid_t> readPid(const char* text) {
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (end == text || *end != '\0' || value <= 0) {
        return std::nullopt;
    }
    return static_cast<pid_t>(value);
}

std::string joinArgs(int argc, char* argv[], int from) {
    std::string result;
    for (int index = from; index < argc; ++index) {
        if (!result.empty()) {
            result += ' ';
        }
        result += argv[index];
    }
    return result;
}

std::vector<chatlab::Draft> takeDrafts(std::deque<chatlab::Draft>& outbox) {
    const std::size_t count = std::min(outbox.size(), chatlab::kDraftsPerRequest);
    std::vector<chatlab::Draft> drafts(outbox.begin(), outbox.begin() + static_cast<std::ptrdiff_t>(count));
    outbox.erase(outbox.begin(), outbox.begin() + static_cast<std::ptrdiff_t>(count));
    return drafts;
}

bool runCommand(const chatlab::Command& command, std::deque<chatlab::Draft>& outbox,
                chatlab::Console& console) {
    using Kind = chatlab::Command::Kind;
    switch (command.kind) {
    case Kind::Public:
    case Kind::Private:
        outbox.push_back({command.to, chatlab::wallMs(), command.text});
        return true;
    case Kind::Who:
        console.roster();
        return true;
    case Kind::Help:
        console.help();
        return true;
    case Kind::Wrong:
        console.problem(command.text);
        return true;
    case Kind::Quit:
        return false;
    case Kind::Nothing:
        return true;
    }
    return true;
}

int lostConnection(chatlab::Console& console, const std::string& failure) {
    chatlab::journal::fail("связь с хостом потеряна: " + failure);
    console.problem("Связь с хостом потеряна: " + failure);
    return EXIT_FAILURE;
}

}

int main(int argc, char* argv[]) {
    chatlab::sigctl::blockForThreads();

    const std::optional<pid_t> host = argc >= 2 ? readPid(argv[1]) : std::nullopt;
    if (!host) {
        std::cerr << "Использование: " << argv[0] << " <pid хоста> [имя]" << std::endl;
        return 2;
    }

    chatlab::journal::open("lab2_client", chatlab::journal::Echo::Silent);
    chatlab::Console console(std::cout);
    chatlab::GuestSession session(*host, joinArgs(argc, argv, 2));

    chatlab::packets::Reply welcome;
    try {
        welcome = session.connect();
    } catch (const chatlab::GuestError& error) {
        chatlab::journal::fail(error.what());
        console.problem(std::string("Не удалось подключиться: ") + error.what());
        return EXIT_FAILURE;
    }
    console.setViewer(session.id());
    console.greeting(*host, chatlab::Conn::Kind(), session.id(), welcome.yourName);
    console.show(welcome);

    chatlab::LineInput input(STDIN_FILENO);
    std::deque<chatlab::Draft> outbox;
    auto nextPoll = Steady::now() + chatlab::kPollPeriod;
    bool staying = true;
    while (staying) {
        if (chatlab::sigctl::quitPending()) {
            break;
        }
        const auto wait = std::max(Steady::duration::zero(), nextPoll - Steady::now());
        for (const std::string& line :
             input.poll(std::chrono::duration_cast<std::chrono::milliseconds>(wait))) {
            staying = runCommand(chatlab::parseCommand(line), outbox, console) && staying;
        }
        if (input.closed() || !staying) {
            break;
        }
        if (Steady::now() < nextPoll) {
            continue;
        }
        const chatlab::Exchange exchange = session.poll(takeDrafts(outbox));
        if (!exchange.ok) {
            return lostConnection(console, exchange.failure);
        }
        console.show(exchange.reply);
        if (exchange.reply.answer == chatlab::packets::Answer::Closed) {
            console.info("Чат закрыт: " + exchange.reply.reason);
            return EXIT_SUCCESS;
        }
        nextPoll = Steady::now() + chatlab::kPollPeriod;
    }

    while (outbox.size() > chatlab::kDraftsPerRequest) {
        const chatlab::Exchange exchange = session.poll(takeDrafts(outbox));
        if (!exchange.ok) {
            return lostConnection(console, exchange.failure);
        }
        console.show(exchange.reply);
    }
    const chatlab::Exchange farewell = session.leave(takeDrafts(outbox));
    if (!farewell.ok) {
        return lostConnection(console, farewell.failure);
    }
    console.show(farewell.reply);
    console.info("Вы вышли из чата");
    chatlab::journal::info("клиент вышел из чата");
    chatlab::journal::close();
    return EXIT_SUCCESS;
}
