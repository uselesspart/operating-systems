#include "conn/conn.h"
#include "gui/HostWindow.h"
#include "host/HostServer.h"
#include "ipc/Signals.h"
#include "util/Log.h"

#include <QApplication>

#include <chrono>
#include <csignal>
#include <cstdlib>
#include <exception>
#include <string>

#include <unistd.h>

namespace {

// CHAT_IDLE_LIMIT_SEC replaces the one-minute inactivity limit (used by the tests)
std::chrono::milliseconds idleLimitFromEnvironment()
{
    const char* value = std::getenv("CHAT_IDLE_LIMIT_SEC");
    if (value == nullptr) {
        return chat::kDefaultIdleLimit;
    }
    char* end = nullptr;
    const long seconds = std::strtol(value, &end, 10);
    if (*end != '\0' || seconds <= 0) {
        return chat::kDefaultIdleLimit;
    }
    return std::chrono::seconds(seconds);
}

}

int main(int argc, char* argv[])
{
    chat::blockProcessSignals();
    std::signal(SIGPIPE, SIG_IGN);
    chat::log::open("chat_host", true);

    QApplication app(argc, argv);
    chat::HostWindow window(chat::Conn::typeCode(), ::getpid());
    chat::HostServer server(window, {idleLimitFromEnvironment()});
    window.setController(&server);

    try {
        server.start();
    } catch (const std::exception& e) {
        chat::log::error(std::string("Cannot start host: ") + e.what());
        return EXIT_FAILURE;
    }

    window.show();
    const int status = app.exec();
    server.stop();
    chat::log::close();
    return status;
}
