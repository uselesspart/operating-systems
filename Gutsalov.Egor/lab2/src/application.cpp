#include "chat.hpp"
#include "connection.hpp"
#include <QApplication>
#include <QCoreApplication>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSocketNotifier>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <charconv>
#include <cerrno>
#include <iostream>
#include <sstream>
#include <unistd.h>

namespace chat {
int run_application(int argc, char** argv, bool headless, const std::string& title,
                    Events& events, const Send& send, const std::function<bool()>& alive) {
    std::unique_ptr<QCoreApplication> application;
    if (headless) application = std::make_unique<QCoreApplication>(argc, argv);
    else application = std::make_unique<QApplication>(argc, argv);
    std::unique_ptr<QWidget> window;
    QPlainTextEdit* history = nullptr;
    QLabel* status = nullptr;
    auto drain = [&] {
        for (const auto& event : events.take()) {
            std::cout << event << std::endl;
            if (history) history->appendPlainText(QString::fromStdString(event));
            if (status && !event.starts_with("MSG ")) status->setText(QString::fromStdString(event));
        }
    };
    if (!headless) {
        window = std::make_unique<QWidget>();
        window->setWindowTitle(QString::fromStdString(title));
        window->resize(850, 550);
        auto* layout = new QVBoxLayout(window.get());
        layout->addWidget(new QLabel(QString::fromStdString(title), window.get()));
        history = new QPlainTextEdit(window.get());
        history->setReadOnly(true);
        history->setMaximumBlockCount(5000);
        history->setObjectName("history");
        layout->addWidget(history);
        auto* row = new QHBoxLayout();
        auto* recipient = new QLineEdit("-1", window.get());
        recipient->setObjectName("recipient");
        recipient->setMaximumWidth(130);
        recipient->setToolTip("-1: общий чат; 0: хост; PID: личное сообщение");
        auto* text = new QLineEdit(window.get());
        text->setObjectName("message");
        text->setPlaceholderText("Сообщение (до 1024 байт UTF-8)");
        auto* button = new QPushButton("Отправить", window.get());
        button->setObjectName("send");
        row->addWidget(new QLabel("Адресат:", window.get()));
        row->addWidget(recipient);
        row->addWidget(text);
        row->addWidget(button);
        layout->addLayout(row);
        status = new QLabel("Подключение…", window.get());
        layout->addWidget(status);
        auto submit = [&, recipient, text] {
            try {
                bool ok = false;
                const int target = recipient->text().toInt(&ok);
                if (!ok || target < -1) throw std::runtime_error("Адресат: -1, 0 или PID клиента");
                send(target, text->text().toStdString());
                text->clear();
            } catch (const std::exception& error) { events.push(std::string("ERROR ") + error.what()); }
        };
        QObject::connect(button, &QPushButton::clicked, application.get(), submit);
        QObject::connect(text, &QLineEdit::returnPressed, application.get(), submit);
        window->show();
    }
    std::unique_ptr<QSocketNotifier> input;
    std::string buffered;
    if (headless) {
        nonblocking(STDIN_FILENO);
        input = std::make_unique<QSocketNotifier>(STDIN_FILENO, QSocketNotifier::Read);
        QObject::connect(input.get(), &QSocketNotifier::activated, application.get(), [&] {
            char bytes[2048];
            const auto size = ::read(STDIN_FILENO, bytes, sizeof(bytes));
            if (size == 0) { input->setEnabled(false); return; }
            if (size < 0) return;
            buffered.append(bytes, static_cast<std::size_t>(size));
            std::size_t newline;
            while ((newline = buffered.find('\n')) != std::string::npos) {
                std::string line = buffered.substr(0, newline);
                buffered.erase(0, newline + 1);
                try {
                    if (line == "/quit") { application->quit(); return; }
                    int target = -1;
                    if (line.starts_with("/to ")) {
                        const auto separator = line.find(' ', 4);
                        if (separator == std::string::npos) throw std::runtime_error("Usage: /to PID text");
                        const auto parsed = std::from_chars(line.data() + 4, line.data() + separator, target);
                        if (parsed.ec != std::errc{} || parsed.ptr != line.data() + separator || target < 0)
                            throw std::runtime_error("Invalid recipient");
                        line.erase(0, separator + 1);
                    }
                    send(target, line);
                } catch (const std::exception& error) { events.push(std::string("ERROR ") + error.what()); }
            }
            if (buffered.size() > max_text + 128) { buffered.clear(); events.push("ERROR Input line too long"); }
        });
    }
    QTimer timer;
    QObject::connect(&timer, &QTimer::timeout, application.get(), [&] {
        drain();
        if (!alive()) application->quit();
    });
    timer.start(20);
    drain();
    const int result = application->exec();
    drain();
    return result;
}
} // namespace chat
