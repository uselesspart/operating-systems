#include "gui/main_window.hpp"
#include "handshake/handshake_signals.hpp"
#include "host/host_controller.hpp"
#include "ipc/connection.hpp"

#include <QApplication>
#include <QTimer>
#include <cerrno>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <pthread.h>
#include <signal.h>
#include <string>
#include <system_error>
#include <unistd.h>

namespace {

sigset_t blockShutdownSignals() {
    sigset_t signalSet{};
    sigemptyset(&signalSet);
    sigaddset(&signalSet, SIGINT);
    sigaddset(&signalSet, SIGTERM);
    const int error = pthread_sigmask(SIG_BLOCK, &signalSet, nullptr);
    if (error != 0) {
        throw std::system_error(error, std::generic_category(), "pthread_sigmask");
    }
    return signalSet;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        lab2::handshake::blockHandshakeSignals();
        const auto shutdownSignals = blockShutdownSignals();
        QApplication app(argc, argv);
        lab2::host::HostController controller;
        const auto transport = std::string(lab2::ipc::transportName());
        const auto pid = getpid();
        lab2::gui::MainWindow window(controller, QString::fromStdString(transport), pid);
        QTimer eventsTimer;
        QObject::connect(&eventsTimer, &QTimer::timeout, &window, [&] {
            const timespec noWait{};
            const int signal = sigtimedwait(&shutdownSignals, nullptr, &noWait);
            if (signal == SIGINT || signal == SIGTERM) {
                controller.requestStop();
                app.quit();
            } else if (signal == -1 && errno != EAGAIN && errno != EINTR) {
                std::cerr << "Error: sigtimedwait failed (" << errno << ")\n";
                controller.requestStop();
                app.exit(EXIT_FAILURE);
            }
        });
        controller.start();
        eventsTimer.start(20);
        std::cout << "Host started\nIPC transport: " << transport << "\nHost PID: " << pid
                  << std::endl;
        window.show();
        const int result = app.exec();
        controller.requestStop();
        controller.join();
        return result;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
