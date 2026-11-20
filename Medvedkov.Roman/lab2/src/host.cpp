#include "ChatSession.hpp"
#include "ChatWindow.hpp"
#include "ChildProcess.hpp"
#include "Connection.hpp"
#include "SharedSession.hpp"

#include <QApplication>
#include <QTimer>
#include <csignal>
#include <iostream>
#include <stdexcept>
#include <sys/prctl.h>
#include <unistd.h>

namespace {
volatile std::sig_atomic_t stopped = 0;
void stopRequested(int) { stopped = 1; }
}

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGTERM, stopRequested);
    std::signal(SIGINT, stopRequested);
    try {
        chat::SharedSession shared;
        auto connection = chat::makeConnection(shared);
        const auto parent = getpid();
        // QApplication must not exist across fork: Qt starts its own threads.
        const auto child = fork();
        if (child < 0) throw std::runtime_error("Cannot fork the client process");
        const bool host = child > 0;
        chat::ChildProcess childGuard(host ? child : 0);
        if (!host) {
            if (prctl(PR_SET_PDEATHSIG, SIGTERM) < 0 || getppid() != parent) return 1;
        }
        const auto role = host ? chat::Party::Host : chat::Party::Client;
        connection->Open(role);
        QApplication app(argc, argv);
        chat::ChatSession session(*connection, shared, role, host ? child : parent);
        chat::ChatWindow window(session, connection->type());
        window.move(host ? 40 : 700, 80);
        window.show();
        QTimer stopTimer;
        QObject::connect(&stopTimer, &QTimer::timeout, &window, [&] { if (stopped) window.close(); });
        stopTimer.start(50);
        return app.exec();
    } catch (const std::exception& error) {
        std::cerr << "chat: " << error.what() << '\n';
        return 1;
    }
}
