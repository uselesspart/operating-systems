#include "app.h"
#include "chat_window.h"
#include "worker.h"

#include <QApplication>
#include <QTimer>
#include <signal.h>

namespace {

volatile sig_atomic_t quit_requested = 0;

void request_quit(int) { quit_requested = 1; }

}

int run_app(int argc, char* argv[], std::unique_ptr<Conn> conn, pid_t host_pid, bool host) {
    Session::block_signals();
    quit_requested = 0;
    struct sigaction action{};
    action.sa_handler = request_quit;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGTERM, &action, nullptr) != 0 || sigaction(SIGINT, &action, nullptr) != 0) {
        system_error("Cannot set signals");
    }
    QApplication app(argc, argv);
    qRegisterMetaType<History>();
    ChatWindow window(host ? "Host" : "Client", QString::fromStdString(conn->type()), host_pid);
    Worker worker(std::move(conn), host_pid, host);
    QObject::connect(&worker, &Worker::status_changed, &window, &ChatWindow::set_status,
                     Qt::QueuedConnection);
    QObject::connect(&worker, &Worker::connected_changed, &window, &ChatWindow::set_connected,
                     Qt::QueuedConnection);
    QObject::connect(&worker, &Worker::history_changed, &window, &ChatWindow::set_history,
                     Qt::QueuedConnection);
    QObject::connect(&window, &ChatWindow::send_requested, &window, [&](int recipient, const QString& text) {
        if (worker.enqueue(recipient, text)) {
            window.clear_input();
        }
    });
    QObject::connect(&worker, &Worker::done, &app, [&](int code) {
        if (!host || code != 0) {
            app.exit(code);
        }
    });
    QObject::connect(&app, &QApplication::aboutToQuit, &worker, &Worker::stop);
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, &app, [&] {
        if (quit_requested) {
            app.quit();
        }
    });
    timer.start(poll_ms);
    window.show();
    worker.start();
    int code = app.exec();
    worker.stop();
    worker.wait();
    return code;
}
