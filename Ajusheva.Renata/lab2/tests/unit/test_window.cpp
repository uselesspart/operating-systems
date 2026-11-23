#include "check.hpp"

#include "gui/ChatWindow.hpp"

#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>

#include <memory>
#include <thread>

using namespace chatlab;

namespace {

QApplication& application() {
    static int argc = 1;
    static char name[] = "unit_tests";
    static char* argv[] = {name, nullptr};
    static QApplication app(argc, argv);
    return app;
}

template <typename Widget>
Widget* part(ChatWindow& window, const char* name) {
    return window.findChild<Widget*>(name);
}

void settle() {
    for (int round = 0; round < 5; ++round) {
        QCoreApplication::processEvents();
    }
}

Post makePost(int from, int to, const std::string& text) {
    Post post;
    post.from = from;
    post.to = to;
    post.fromName = from == 0 ? "Хост" : "Алиса";
    post.toName = to == kEveryone ? "" : (to == 0 ? "Хост" : "Борис");
    post.text = text;
    return post;
}

}

TEST(window_shows_roster_and_recipients) {
    application();
    ChatWindow window("sock", 4242);
    window.onRoster({{0, "Хост"}, {1, "Алиса"}, {2, "Борис"}});
    settle();
    CHECK_EQ(part<QListWidget>(window, "members")->count(), 3);
    CHECK_EQ(part<QComboBox>(window, "target")->count(), 3);
    CHECK(part<QLabel>(window, "counter")->text().contains("клиентов: 2"));
    CHECK(part<QLabel>(window, "counter")->text().contains("sock"));
    CHECK(window.windowTitle().contains("4242"));
}

TEST(window_events_from_other_threads_arrive) {
    application();
    ChatWindow window("fifo", 1);
    std::thread worker([&window] {
        window.onStatus("Клиент 1 присоединился");
        window.onPost(makePost(1, kEveryone, "из потока"));
    });
    worker.join();
    settle();
    CHECK(part<QPlainTextEdit>(window, "journal")->toPlainText().contains("Клиент 1 присоединился"));
    CHECK(part<QPlainTextEdit>(window, "feed")->toPlainText().contains("Алиса: из потока"));
}

TEST(window_private_posts_are_marked) {
    application();
    ChatWindow window("pipe", 1);
    window.onPost(makePost(1, 0, "лично хосту"));
    settle();
    CHECK(part<QPlainTextEdit>(window, "feed")->toPlainText().contains("Алиса -> вам (лично): лично хосту"));
}

TEST(window_send_uses_selected_recipient) {
    application();
    ChatWindow window("sock", 1);
    int sentTo = -100;
    std::string sentText;
    window.setSendHandler([&](int to, const std::string& text) {
        sentTo = to;
        sentText = text;
    });
    window.onRoster({{0, "Хост"}, {7, "Вера"}});
    settle();

    part<QLineEdit>(window, "input")->setText("  всем  ");
    part<QPushButton>(window, "send")->click();
    CHECK_EQ(sentTo, kEveryone);
    CHECK_EQ(sentText, std::string("всем"));
    CHECK(part<QLineEdit>(window, "input")->text().isEmpty());

    QComboBox* target = part<QComboBox>(window, "target");
    target->setCurrentIndex(target->findData(7));
    part<QLineEdit>(window, "input")->setText("Вере");
    part<QPushButton>(window, "send")->click();
    CHECK_EQ(sentTo, 7);

    sentText.clear();
    part<QLineEdit>(window, "input")->setText("   ");
    part<QPushButton>(window, "send")->click();
    CHECK(sentText.empty());
}

TEST(window_keeps_recipient_when_roster_changes) {
    application();
    ChatWindow window("sock", 1);
    window.onRoster({{0, "Хост"}, {3, "А"}, {4, "Б"}});
    settle();
    QComboBox* target = part<QComboBox>(window, "target");
    target->setCurrentIndex(target->findData(4));
    window.onRoster({{0, "Хост"}, {4, "Б"}});
    settle();
    CHECK_EQ(target->currentData().toInt(), 4);
    window.onRoster({{0, "Хост"}});
    settle();
    CHECK_EQ(target->currentData().toInt(), kEveryone);
}
