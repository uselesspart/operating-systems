#include "session.hpp"

#include <QApplication>
#include <QCheckBox>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

#include <csignal>
#include <cstdio>
#include <optional>

namespace {
volatile std::sig_atomic_t closing = 0;
void closeSignal(int) { closing = 1; }

QString fromText(const Frame& value) {
    return QString::fromStdString(message(value));
}
}

int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT, closeSignal);
    std::signal(SIGTERM, closeSignal);
    try {
        Session session;
        QApplication app(argc, argv);
        QWidget window;
        window.setWindowTitle("Локальный чат — " CHAT_TRANSPORT);
        window.resize(620, 460);
        auto* layout = new QVBoxLayout(&window);
        auto* history = new QPlainTextEdit;
        history->setReadOnly(true);
        history->setMaximumBlockCount(1000);
        auto* status = new QLabel("Хост запущен. Ожидание клиента.");
        auto* input = new QLineEdit;
        input->setPlaceholderText("Текст сообщения");
        auto* personal = new QCheckBox("Лично клиенту");
        auto* send = new QPushButton("Отправить");
        input->setEnabled(false);
        send->setEnabled(false);
        layout->addWidget(history);
        layout->addWidget(input);
        layout->addWidget(personal);
        layout->addWidget(send);
        layout->addWidget(status);

        bool connected = false;
        bool active = true;
        auto lastMessage = Clock::now();
        std::optional<Frame> outgoing;
        const auto submit = [&] {
            if (!active || !connected || outgoing || input->text().trimmed().isEmpty()) return;
            const auto line = (personal->isChecked() ? "[Личное] Хост: " : "[Общее] Хост: ") + input->text();
            try {
                outgoing = frame('D', line.toStdString());
                input->clear();
                send->setEnabled(false);
            } catch (const std::exception& error) {
                status->setText(QString::fromUtf8(error.what()));
            }
        };
        QObject::connect(send, &QPushButton::clicked, &window, submit);
        QObject::connect(input, &QLineEdit::returnPressed, &window, submit);

        const auto disconnect = [&](const QString& reason, int signal) {
            std::fprintf(stderr, "%s\n", reason.toUtf8().constData());
            status->setText(reason);
            history->appendPlainText(reason);
            input->setEnabled(false);
            send->setEnabled(false);
            active = false;
            session.stop(signal);
        };

        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &window, [&] {
            if (closing) { app.quit(); return; }
            if (!active) return;
            try {
                Frame received{};
                if (session.receive(received)) {
                    switch (received[0]) {
                    case 'C':
                        connected = true;
                        lastMessage = Clock::now();
                        status->setText("Клиент подключён");
                        history->appendPlainText(status->text());
                        input->setEnabled(true);
                        send->setEnabled(true);
                        break;
                    case 'A':
                    case 'P':
                        lastMessage = Clock::now();
                        history->appendPlainText(
                            (received[0] == 'A' ? "[Общее] Клиент: " : "[Личное] Клиент: ")
                            + fromText(received));
                        break;
                    case 'Q':
                        disconnect("Клиент завершил работу", SIGTERM);
                        return;
                    case 'N': break;
                    default: throw std::runtime_error("Неизвестный тип сообщения");
                    }
                }
                if (connected && Clock::now() - lastMessage >= 60s) {
                    disconnect("Клиент отключён: минута без сообщений (SIGKILL)", SIGKILL);
                    return;
                }
                if (!session.waiting()) {
                    if (outgoing) {
                        session.send(*outgoing);
                        history->appendPlainText(fromText(*outgoing));
                        outgoing.reset();
                        send->setEnabled(true);
                    } else {
                        session.send(frame('N'));
                    }
                }
            } catch (const std::exception& error) {
                disconnect(QString::fromUtf8(error.what()), SIGTERM);
            }
        });
        timer.start(50);
        window.show();
        return app.exec();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "Хост: %s\n", error.what());
        return 1;
    }
}
