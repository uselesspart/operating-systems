#include <QApplication>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>

#include <thread>

#include "gui/HostWindow.h"
#include "test.h"

TEST_SUITE("HostWindow — окно волка (Qt, offscreen)");

using wolf::HostWindow;
using wolf::KidInfo;
using wolf::proto::Outcome;

namespace {

QApplication& app() {
    static int argc = 1;
    static char name[] = "test_hostwindow";
    static char* argv[] = {name, nullptr};
    qputenv("QT_QPA_PLATFORM", "offscreen");
    static QApplication instance(argc, argv);
    return instance;
}

void pump() {
    for (int i = 0; i < 5; ++i) QApplication::processEvents();
}

template <typename T>
T* child(HostWindow& w, const char* name) {
    return w.findChild<T*>(name);
}

std::string text(QLabel* l) { return l ? l->text().toStdString() : "<нет виджета>"; }

wolf::game::RoundResult sampleRound() {
    wolf::game::RoundResult r;
    r.round = 3;
    r.wolf = 40;
    r.kids = 2;
    r.hideRange = 35;
    r.resurrectRange = 10;
    r.results = {{1, 57, 17, true, true, Outcome::Hidden}, {2, 90, 50, true, false, Outcome::Caught}};
    r.hidden = 1;
    r.caught = 1;
    r.dead = 1;
    return r;
}

}

TEST(widgets_present, "есть ввод числа, кнопки, таблица козлят, ход игры и статусы") {
    app();
    HostWindow w("fifo", 1234);
    EXPECT_TRUE(child<QSpinBox>(w, "wolfNumber") != nullptr);
    EXPECT_TRUE(child<QPushButton>(w, "throwButton") != nullptr);
    EXPECT_TRUE(child<QPushButton>(w, "addKidButton") != nullptr);
    EXPECT_TRUE(child<QTableWidget>(w, "kidsTable") != nullptr);
    EXPECT_TRUE(child<QPlainTextEdit>(w, "historyView") != nullptr);
    EXPECT_TRUE(child<QPlainTextEdit>(w, "statusView") != nullptr);
    EXPECT_CONTAINS(w.windowTitle().toStdString(), "1234");
    auto* spin = child<QSpinBox>(w, "wolfNumber");
    ASSERT_TRUE(spin != nullptr);
    EXPECT_EQ(spin->minimum(), 1);
    EXPECT_EQ(spin->maximum(), 100);
}

TEST(user_number_submitted, "введённое число уходит серверу по кнопке «Бросить»") {
    app();
    HostWindow w("sock", 1);
    int received = 0;
    w.onWolfNumber = [&](int n) { received = n; };
    w.onTurnStarted(1, std::chrono::milliseconds(3000));
    pump();
    child<QSpinBox>(w, "wolfNumber")->setValue(73);
    child<QPushButton>(w, "throwButton")->click();
    EXPECT_EQ(received, 73);
    EXPECT_CONTAINS(text(child<QLabel>(w, "turnLabel")), "73");
}

TEST(add_kid_button, "кнопка «Добавить козлёнка» вызывает сервер") {
    app();
    HostWindow w("pipe", 1);
    int calls = 0;
    w.onAddKid = [&] { ++calls; };
    child<QPushButton>(w, "addKidButton")->click();
    EXPECT_EQ(calls, 1);
}

TEST(turn_countdown_shown, "во время хода показан обратный отсчёт") {
    app();
    HostWindow w("pipe", 1);
    w.onTurnStarted(4, std::chrono::milliseconds(3000));
    pump();
    EXPECT_CONTAINS(text(child<QLabel>(w, "stateLabel")), "Раунд 4");
    EXPECT_CONTAINS(text(child<QLabel>(w, "turnLabel")), "осталось");
}

TEST(round_displayed, "итог раунда: числа, спрятались/попались/мертвы") {
    app();
    HostWindow w("fifo", 1);
    w.onRound(sampleRound(), wolf::WolfSource::User);
    pump();
    const std::string stats = text(child<QLabel>(w, "statsLabel"));
    EXPECT_CONTAINS(stats, "Раунд 3");
    EXPECT_CONTAINS(stats, "волк 40 (введено)");
    EXPECT_CONTAINS(stats, "спрятались 1");
    EXPECT_CONTAINS(stats, "попались 1");
    EXPECT_CONTAINS(stats, "мертвы 1");
    const std::string history = child<QPlainTextEdit>(w, "historyView")->toPlainText().toStdString();
    EXPECT_CONTAINS(history, "#1: 57 → спрятался");
    EXPECT_CONTAINS(history, "#2: 90 → попался");
}

TEST(kids_table, "таблица козлят: номер, pid, жив/мёртв, бросок, итог") {
    app();
    HostWindow w("fifo", 1);
    w.onKids({KidInfo{1, 111, true, 57, Outcome::Hidden},
              KidInfo{2, 222, false, 90, Outcome::Caught}});
    pump();
    auto* t = child<QTableWidget>(w, "kidsTable");
    ASSERT_EQ(t->rowCount(), 2);
    EXPECT_EQ(t->item(0, 0)->text().toStdString(), "#1");
    EXPECT_EQ(t->item(0, 1)->text().toStdString(), "111");
    EXPECT_EQ(t->item(0, 2)->text().toStdString(), "жив");
    EXPECT_EQ(t->item(1, 2)->text().toStdString(), "мёртв");
    EXPECT_EQ(t->item(1, 4)->text().toStdString(), "попался");
    EXPECT_CONTAINS(text(child<QLabel>(w, "kidsLabel")), "Козлят: 2, живых: 1");
}

TEST(status_messages, "статусные сообщения и конец игры появляются в окне") {
    app();
    HostWindow w("fifo", 1);
    w.onStatus("козлёнок #1 (pid 5) присоединился");
    w.onGameOver(7);
    pump();
    const std::string log = child<QPlainTextEdit>(w, "statusView")->toPlainText().toStdString();
    EXPECT_CONTAINS(log, "присоединился");
    EXPECT_CONTAINS(text(child<QLabel>(w, "stateLabel")), "Игра окончена на раунде 7");
}

TEST(observer_from_other_thread, "вызовы из чужого потока доходят до окна") {
    app();
    HostWindow w("fifo", 1);
    std::thread t([&] { w.onStatus("из потока сервера"); });
    t.join();
    pump();
    EXPECT_CONTAINS(child<QPlainTextEdit>(w, "statusView")->toPlainText().toStdString(),
                    "из потока сервера");
}
