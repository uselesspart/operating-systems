#include "chat.hpp"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <QTimer>
#include <iostream>
#include <unistd.h>

int main(int argc, char** argv) {
    try {
        prepareSignals();
        QCoreApplication app(argc, argv);
        const auto event = [&app](const QJsonObject& value) {
            QMetaObject::invokeMethod(&app, [&app, value] {
                std::cout << QJsonDocument(value).toJson(QJsonDocument::Compact).constData() << std::endl;
                if (value["type"] == "closed") app.exit(1);
            }, Qt::QueuedConnection);
        };
        std::unique_ptr<Endpoint> endpoint;
        if (argc == 1) endpoint = std::make_unique<Server>(event);
        else endpoint = std::make_unique<Client>(parsePid(argv[1]), event);
        QByteArray pending;
        QSocketNotifier input(STDIN_FILENO, QSocketNotifier::Read);
        QObject::connect(&input, &QSocketNotifier::activated, &app, [&] {
            char buffer[BUFSIZ];
            const ssize_t count = read(STDIN_FILENO, buffer, sizeof(buffer));
            if (count <= 0) { app.quit(); return; }
            pending.append(buffer, count);
            qsizetype end;
            while ((end = pending.indexOf('\n')) >= 0) {
                const auto command = QJsonDocument::fromJson(pending.left(end)).object();
                pending.remove(0, end + 1);
                if (command["type"] == "quit") { app.quit(); return; }
                try {
                    endpoint->Send(command["recipient"].toInt(), command["text"].toString());
                } catch (const std::exception& error) {
                    event({{"type", "error"}, {"text", error.what()}});
                }
            }
        });
        QTimer signalWatcher;
        QObject::connect(&signalWatcher, &QTimer::timeout, &app, [&app] {
            auto set = signalSet(SIGTERM);
            sigaddset(&set, SIGINT);
            const timespec timeout{0, 0};
            if (sigtimedwait(&set, nullptr, &timeout) > 0) app.quit();
        });
        signalWatcher.start(static_cast<int>(pollInterval.count()));
        const int result = app.exec();
        endpoint->Stop();
        return result;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
