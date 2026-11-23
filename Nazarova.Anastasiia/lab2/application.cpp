#include "chat.hpp"
#include "window.hpp"

#include <QApplication>
#include <QTimer>
#include <iostream>
#include <unistd.h>

int runApplication(bool host, int argc, char** argv) {
    try {
        if (argc != (host ? 1 : 2)) {
            std::cerr << "Использование: " << argv[0] << (host ? "" : " PID_хоста") << '\n';
            return 1;
        }
        const pid_t hostPid = host ? getpid() : parsePid(argv[1]);
        prepareSignals();
        QApplication app(argc, argv);
        ChatWindow window(hostPid, getpid());
        const auto handler = [&window, &app](const QJsonObject& event) {
            QMetaObject::invokeMethod(&window, [&window, &app, event] {
                window.Handle(event);
                if (event["type"] != "chat") {
                    std::cerr << event["text"].toString().toStdString() << '\n';
                }
                if (event["type"] == "closed") app.exit(1);
            }, Qt::QueuedConnection);
        };
        std::unique_ptr<Endpoint> endpoint;
        if (host) endpoint = std::make_unique<Server>(handler);
        else endpoint = std::make_unique<Client>(hostPid, handler);
        window.SetSender([&endpoint](pid_t recipient, const QString& text) { endpoint->Send(recipient, text); });
        QTimer signalWatcher;
        QObject::connect(&signalWatcher, &QTimer::timeout, &app, [&app] {
            sigset_t set = signalSet(SIGTERM);
            sigaddset(&set, SIGINT);
            const timespec timeout{0, 0};
            if (sigtimedwait(&set, nullptr, &timeout) > 0) app.quit();
        });
        signalWatcher.start(static_cast<int>(pollInterval.count()));
        window.show();
        const int result = app.exec();
        endpoint->Stop();
        return result;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
