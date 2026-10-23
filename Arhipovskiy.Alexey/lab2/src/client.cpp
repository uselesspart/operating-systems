#include "client/ChatClient.h"
#include "client/Commands.h"
#include "client/ConsoleUi.h"
#include "client/LineReader.h"
#include "client/Utf8Terminal.h"
#include "conn/conn.h"
#include "ipc/Signals.h"
#include "util/Log.h"

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string>

#include <unistd.h>

namespace {

constexpr auto kInputPollInterval = std::chrono::milliseconds(200);

std::optional<pid_t> parsePid(const char* text)
{
    char* end = nullptr;
    const long value = std::strtol(text, &end, 10);
    if (*end != '\0' || value <= 0) {
        return std::nullopt;
    }
    return static_cast<pid_t>(value);
}

// Returns false when the user asked to leave
bool execute(const chat::Command& command, chat::ChatClient& client, chat::ConsoleUi& ui)
{
    using Kind = chat::Command::Kind;
    switch (command.kind) {
    case Kind::Say:
    case Kind::Private:
        client.send(command.to, command.text);
        break;
    case Kind::Who:
        ui.printParticipants(client.participants());
        break;
    case Kind::Help:
        ui.printHelp();
        break;
    case Kind::Invalid:
        ui.printError(command.text);
        break;
    case Kind::Quit:
        return false;
    case Kind::Empty:
        break;
    }
    return true;
}

}

int main(int argc, char* argv[])
{
    chat::blockProcessSignals();
    std::signal(SIGPIPE, SIG_IGN);

    const std::optional<pid_t> hostPid = argc >= 2 && argc <= 3 ? parsePid(argv[1]) : std::nullopt;
    if (!hostPid) {
        std::cerr << "Использование: " << argv[0] << " <pid хоста> [имя]\n";
        return 2;
    }

    const chat::Utf8Terminal terminal(STDIN_FILENO);
    chat::log::open("chat_client", false);
    chat::ConsoleUi ui(std::cout);
    chat::ChatClient client(*hostPid, argc == 3 ? argv[2] : "", ui);
    try {
        client.connect();
    } catch (const chat::ClientError& e) {
        chat::log::error(e.what());
        ui.printError(e.what());
        return EXIT_FAILURE;
    }
    ui.setSelfId(client.id());
    ui.printConnected(*hostPid, chat::Conn::typeCode(), client.id(), client.name());
    client.start();

    chat::LineReader input(STDIN_FILENO);
    std::string line;
    while (client.running()) {
        if (chat::takeTerminationRequest()) {
            break;
        }
        const auto status = input.readLine(line, kInputPollInterval);
        if (status == chat::LineReader::Status::End) {
            break;
        }
        if (status == chat::LineReader::Status::Line && !execute(chat::parseCommand(line), client, ui)) {
            break;
        }
    }

    client.leave();
    chat::log::close();
    return client.lostConnection() ? EXIT_FAILURE : EXIT_SUCCESS;
}
