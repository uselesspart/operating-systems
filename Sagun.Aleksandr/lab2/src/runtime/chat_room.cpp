#include "sessions.hpp"
#include "client_reader.hpp"
#include "ui/chat_window.hpp"

#include <QApplication>
#include <QDeadlineTimer>
#include <QTimer>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <deque>
#include <string>
#include <system_error>

namespace {

const QString errorPrefix = QStringLiteral("E\n");

struct Pending {
    std::string text;
    QDeadlineTimer deadline{5000};
};

struct Delivery {
    std::unique_ptr<ClientReader> reader;
    std::deque<Pending> queue;
};

} // namespace

int runClient(int& argc, char** argv, int self, int clientCount, Connection& channel)
{
    QApplication app(argc, argv);
    ChatWindow window(self, clientCount);
    channel.listen(&window,
        [&](const std::string& bytes) {
            const auto packet = QString::fromStdString(bytes);
            if (packet.startsWith(errorPrefix)) {
                window.appendLine(packet.mid(errorPrefix.size()));
                return;
            }
            const auto message = chat::decodeMessage(packet);
            if (!message || message->number == 0) {
                window.appendLine(QStringLiteral("Некорректное сообщение хоста"));
                return;
            }
            window.showMessage(*message);
        },
        [&](const std::string& reason) {
            if (!reason.empty()) std::fprintf(stderr, "client: %s\n", reason.c_str());
            app.quit();
        });

    QObject::connect(&window, &ChatWindow::sendRequested, &window,
        [&](const chat::Message& message) {
            try {
                channel.send(chat::encodeMessage(message).toStdString());
                window.clearInput(); // Строку в историю доставит хост.
            } catch (const std::exception& e) {
                window.appendLine(QStringLiteral("Не отправлено: ") + QString::fromUtf8(e.what()));
            }
        });
    channel.send("HELLO");
    window.show();
    return app.exec();
}

int runHost(int& argc, char** argv, Sessions& sessions)
{
    QApplication app(argc, argv);
    ChatWindow window(chat::host, static_cast<int>(sessions.size()));
    std::vector<Delivery> deliveries(sessions.size());
    quint64 sequence = 0;

    auto disconnect = [&](std::size_t i, const QString& reason) {
        auto& s = *sessions[i];
        if (!s.online) return;
        s.online = false;
        deliveries[i].reader.reset(); // Дождаться выхода потока перед close().
        deliveries[i].queue.clear();
        s.channel->close();
        window.appendLine(s.name + QStringLiteral(": ") + reason);
        for (const auto& other : sessions) {
            if (other->online) return;
        }
        window.stopSending();
    };

    auto flush = [&](std::size_t i) {
        auto& queue = deliveries[i].queue;
        while (!queue.empty()) {
            if (queue.front().deadline.hasExpired()) {
                disconnect(i, QStringLiteral("истёк срок доставки сообщения"));
                return;
            }
            try {
                sessions[i]->channel->send(queue.front().text);
                queue.pop_front();
            } catch (const std::system_error& e) {
                if (e.code().value() == EAGAIN || e.code().value() == EWOULDBLOCK) return;
                disconnect(i, QString::fromUtf8(e.what()));
                return;
            } catch (const std::exception& e) {
                disconnect(i, QString::fromUtf8(e.what()));
                return;
            }
        }
    };

    // У каждого клиента своя очередь. Индекс сессии на единицу меньше ID.
    auto sendToClient = [&](int id, const std::string& bytes) {
        const auto i = static_cast<std::size_t>(id - 1);
        if (i >= sessions.size() || !sessions[i]->online) return false;
        auto& queue = deliveries[i].queue;
        if (queue.size() >= 128) {
            disconnect(i, QStringLiteral("переполнена очередь доставки"));
            return false;
        }
        queue.push_back(Pending{bytes});
        flush(i);
        return sessions[i]->online;
    };

    auto route = [&](chat::Message message) {
        message.number = ++sequence;
        const auto bytes = chat::encodeMessage(message).toStdString();
        if (message.to == chat::everyone) {
            window.showMessage(message);
            for (std::size_t i = 0; i < sessions.size(); ++i) {
                sendToClient(static_cast<int>(i + 1), bytes);
            }
            return true;
        }

        // Сначала доставляем адресату. Затем одну копию получает отправитель.
        if (message.to == chat::host) {
            window.showMessage(message);
        } else if (!sendToClient(message.to, bytes)) {
            const QString reason = QStringLiteral("Не отправлено: %1 недоступен")
                .arg(chat::participantName(message.to));
            if (message.from == chat::host) window.appendLine(reason);
            else sendToClient(message.from, (errorPrefix + reason).toStdString());
            return false;
        }
        if (message.from == chat::host) window.showMessage(message);
        else sendToClient(message.from, bytes);
        return true;
    };

    for (std::size_t i = 0; i < sessions.size(); ++i) {
        auto& d = deliveries[i];
        d.reader = std::make_unique<ClientReader>(*sessions[i]->channel);
        d.reader->setObjectName(QStringLiteral("chat-reader-%1").arg(i + 1));
        QObject::connect(d.reader.get(), &ClientReader::received, &window,
            [&, i](const QString& packet) {
                auto& s = *sessions[i];
                if (!s.online) return;
                if (!s.greeted) {
                    if (packet != QStringLiteral("HELLO")) {
                        disconnect(i, QStringLiteral("ошибка первого сообщения"));
                        return;
                    }
                    s.greeted = true;
                    window.appendLine(s.name + QStringLiteral(" подключился"));
                    return;
                }
                const auto message = chat::decodeMessage(packet);
                const int id = static_cast<int>(i + 1);
                if (!message || message->from != id || message->number != 0 ||
                    message->to == id || message->to > static_cast<int>(sessions.size())) {
                    disconnect(i, QStringLiteral("некорректное сообщение клиента"));
                    return;
                }
                s.idle.restart();
                route(*message);
            }, Qt::QueuedConnection);
        QObject::connect(d.reader.get(), &ClientReader::disconnected, &window,
            [&, i](const QString& reason) {
                disconnect(i, reason.isEmpty() ? QStringLiteral("отключился") : reason);
            }, Qt::QueuedConnection);
        d.reader->start();
    }

    QObject::connect(&window, &ChatWindow::sendRequested, &window,
        [&](const chat::Message& message) {
            if (route(message)) window.clearInput();
        });

    QTimer watchdog;
    QObject::connect(&watchdog, &QTimer::timeout, &window, [&] {
        for (std::size_t i = 0; i < sessions.size(); ++i) {
            auto& s = *sessions[i];
            const bool alive = stillRunning(s); // Забираем статус и у отключённых.
            if (!s.online) continue;
            if (alive && s.idle.elapsed() > 60000) {
                if (kill(s.pid, SIGKILL) == 0) {
                    disconnect(i, QStringLiteral("отключён за минуту без сообщений"));
                } else if (errno != ESRCH) {
                    std::perror("kill");
                }
            } else {
                flush(i);
            }
        }
    });
    watchdog.start(200);
    window.show();
    return app.exec();
}
