#include "Check.hpp"
#include "ChatWindow.hpp"
#include "ChildProcess.hpp"
#include "FileDescriptor.hpp"

#include <QApplication>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <QTextBrowser>
#include <QTimer>
#include <csignal>
#include <fcntl.h>
#include <sys/wait.h>

using namespace chat;

namespace {
void typeAndSend(ChatWindow& window, const QString& text, bool personal, bool enter) {
    auto* input = window.findChild<QLineEdit*>("messageInput");
    auto* recipients = window.findChild<QComboBox*>("recipientBox");
    auto* button = window.findChild<QPushButton*>("sendButton");
    expect(input && recipients && button, "Widgets are present");
    recipients->setCurrentIndex(personal ? 1 : 0);
    input->setText(text);
    expect(button->isEnabled(), "Valid message can be sent");
    if (enter) QTest::keyClick(input, Qt::Key_Return);
    else QTest::mouseClick(button, Qt::LeftButton);
    expect(input->text().isEmpty(), "Editor cleared after submit");
}

void checkHistory(ChatWindow& window) {
    auto* history = window.findChild<QTextBrowser*>("messageHistory");
    expect(history != nullptr, "History widget exists");
    const auto text = history->toPlainText();
    for (const auto& body : {"host_public", "<b>client & public</b>", "host_private", "client_private"}) {
        expect(text.count(QString::fromUtf8(body)) == 1, "Each message appears exactly once");
    }
    expect(text.indexOf("#1") < text.indexOf("#2") && text.indexOf("#2") < text.indexOf("#3") &&
           text.indexOf("#3") < text.indexOf("#4"), "GUI displays total submission order");
    expect(!text.contains("#5"), "Invalid input did not reserve a sequence");
}
}

int main(int argc, char* argv[]) {
    std::signal(SIGPIPE, SIG_IGN);
    try {
        SharedSession shared;
        auto connection = makeConnection(shared);
        int report[2];
        if (pipe2(report, O_NONBLOCK | O_CLOEXEC) < 0) throw std::runtime_error("pipe2 test report");
        FileDescriptor reader(report[0]), writer(report[1]);
        const auto parent = getpid();
        const auto child = fork();
        if (child < 0) throw std::runtime_error("fork GUI test");
        const bool host = child > 0;
        ChildProcess guard(host ? child : 0);
        if (host) writer.reset(); else reader.reset();
        connection->Open(host ? Party::Host : Party::Client);
        int result = 0;
        {
            QApplication app(argc, argv);
            ChatSession session(*connection, shared, host ? Party::Host : Party::Client, host ? child : parent);
            ChatWindow window(session, connection->type());
            window.show();
            auto safe = [&](auto action) {
                return [&, action] {
                    try { action(); } catch (const std::exception& error) {
                        std::cerr << "GUI: " << error.what() << '\n'; result = 1; app.quit();
                    }
                };
            };
            if (host) {
                QTimer::singleShot(60, &window, safe([&] { typeAndSend(window, "host_public", false, true); }));
                QTimer::singleShot(340, &window, safe([&] { typeAndSend(window, "host_private", true, false); }));
                QTimer::singleShot(2300, &window, safe([&] {
                    checkHistory(window);
                    expect(!session.connected(), "Host detects child exit");
                    expect(!window.findChild<QPushButton*>("sendButton")->isEnabled(), "Disconnected editor disabled");
                    char status = 0;
                    expect(read(reader.get(), &status, 1) == 1 && status == '1', "Child GUI checks passed");
                    app.quit();
                }));
            } else {
                QTimer::singleShot(180, &window, safe([&] { typeAndSend(window, "<b>client & public</b>", false, false); }));
                QTimer::singleShot(500, &window, safe([&] { typeAndSend(window, "client_private", true, true); }));
                QTimer::singleShot(800, &window, safe([&] {
                    auto* input = window.findChild<QLineEdit*>("messageInput");
                    input->setText(QString(500, QChar(0x044f)));
                    expect(!window.findChild<QPushButton*>("sendButton")->isEnabled(), "Limit counts UTF-8 bytes");
                    input->setText("   ");
                    expect(!window.findChild<QPushButton*>("sendButton")->isEnabled(), "Whitespace-only text disabled");
                    input->clear();
                }));
                QTimer::singleShot(1600, &window, safe([&] { checkHistory(window); app.quit(); }));
            }
            QTimer::singleShot(10000, &window, [&] { result = 1; app.quit(); });
            app.exec();
        }
        if (!host) {
            if (write(writer.get(), result == 0 ? "1" : "0", 1) != 1) return 1;
        }
        if (result == 0) std::cout << (host ? "Host" : "Client") << " GUI: " << checks << " checks passed\n";
        return result;
    } catch (const std::exception& error) { std::cerr << "GUI FAILED: " << error.what() << '\n'; return 1; }
}
