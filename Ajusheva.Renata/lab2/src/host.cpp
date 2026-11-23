#include "conn/conn.hpp"
#include "gui/ChatWindow.hpp"
#include "host/Server.hpp"
#include "link/Signals.hpp"
#include "util/Journal.hpp"

#include <QApplication>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <string>

#include <unistd.h>

namespace {

std::chrono::milliseconds silenceLimit() {
    const char* value = std::getenv("LAB2_SILENCE_SEC");
    if (value == nullptr) {
        return chatlab::kSilenceLimit;
    }
    char* end = nullptr;
    const long seconds = std::strtol(value, &end, 10);
    if (end == value || *end != '\0' || seconds <= 0) {
        return chatlab::kSilenceLimit;
    }
    return std::chrono::seconds(seconds);
}

}

int main(int argc, char* argv[]) {
    chatlab::sigctl::blockForThreads();
    chatlab::journal::open("lab2_host", chatlab::journal::Echo::Stderr);

    QApplication app(argc, argv);
    chatlab::ChatWindow window(chatlab::Conn::Kind(), ::getpid());
    chatlab::Server server(window, {silenceLimit()});
    window.setSendHandler([&server](int to, const std::string& text) { server.say(to, text); });

    try {
        server.start();
    } catch (const std::exception& error) {
        chatlab::journal::fail(std::string("хост не запущен: ") + error.what());
        return EXIT_FAILURE;
    }

    window.show();
    const int code = app.exec();
    server.stop();
    chatlab::journal::close();
    return code;
}
