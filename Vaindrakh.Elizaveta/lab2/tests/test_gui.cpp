#include "window.hpp"
#include <QApplication>
#include <QLabel>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QTest>
#include <chrono>
#include <functional>
#include <iostream>
#include <unistd.h>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
bool until(const std::function<bool()>& condition, int timeout) {
    QElapsedTimer clock;
    clock.start();
    while (!condition() && clock.elapsed() < timeout) QTest::qWait(10);
    return condition();
}
}
int main(int argc, char** argv) {
    const std::string log = "/tmp/wolf-gui-test-" + std::to_string(getpid()) + ".log";
    try {
        lab::Options options;
        options.seed = 1;
        options.logPath = log;
        options.wolves = {100, 100};
        options.goats = {1, 1};
        lab::Session session(options); // Must precede QApplication.
        QApplication app(argc, argv);
        lab::Window window(session);
        window.show();
        auto* button = window.findChild<QPushButton*>("submitTurn");
        auto* spin = window.findChild<QSpinBox*>("wolfNumber");
        auto* table = window.findChild<QTableWidget*>("rounds");
        auto* status = window.findChild<QLabel*>("status");
        check(button && spin && table && status, "Required GUI controls missing");
        check(!button->isEnabled() && table->rowCount() == 0, "Game started before connection");
        check(spin->minimum() == 1 && spin->maximum() == 100, "Number range invalid");
        check(until([&] { return button->isEnabled(); }, 2000), "Client not connected");
        spin->setValue(100);
        QTest::mouseClick(button, Qt::LeftButton);
        check(until([&] { return table->rowCount() == 1 && button->isEnabled(); }, 2000), "Manual turn failed");
        check(table->item(0, 1)->text() == "100" && table->item(0, 2)->text() == "1", "Wrong rolls in GUI");
        check(table->item(0, 4)->text() == "1" && table->item(0, 6)->text() == "1", "Wrong caught/dead counts");
        QElapsedTimer timer;
        timer.start();
        check(until([&] { return table->rowCount() == 2; }, 4500), "Automatic 3-second turn failed");
        check(timer.elapsed() >= 2700 && timer.elapsed() < 4200, "Automatic input deadline is wrong");
        check(status->text().contains(QString::fromUtf8("Игра завершена")), "Game-over status missing");
        check(!button->isEnabled() && session.childPid() == -1 && !window.failed(), "Finished client was not reaped");
        const char* screenshot = std::getenv("LAB2_SCREENSHOT");
        if (screenshot && std::string(TYPE_CODE) == "mmap") check(window.grab().save(screenshot), "Cannot save screenshot");
        window.close();
        unlink(log.c_str());
        std::cout << TYPE_CODE " GUI: connect, manual input, 3-second fallback, table, finish: OK\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        unlink(log.c_str());
        return 1;
    }
}
