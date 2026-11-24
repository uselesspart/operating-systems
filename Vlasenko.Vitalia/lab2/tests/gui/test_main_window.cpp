#include "gui/main_window.hpp"
#include "handshake/handshake_signals.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QLabel>
#include <QPlainTextEdit>
#include <QProcess>
#include <QProgressBar>
#include <QPushButton>
#include <QSpinBox>
#include <QTableWidget>
#include <QtTest/QtTest>
#include <unistd.h>

namespace {
using lab2::game::KidOutcome;
using lab2::game::KidState;
using lab2::host::HostEventType;

class MainWindowTest : public QObject {
    Q_OBJECT

private Q_SLOTS:
    void initialState() {
        lab2::host::HostController controller;
        lab2::gui::MainWindow window(controller, "fifo", 123);
        auto* number = window.findChild<QSpinBox*>("wolfNumber");
        QVERIFY(number != nullptr);
        QCOMPARE(number->minimum(), 1);
        QCOMPARE(number->maximum(), 100);
        QVERIFY(!number->isEnabled());
        QVERIFY(!window.findChild<QPushButton*>("submitButton")->isEnabled());
        QCOMPARE(window.findChild<QProgressBar*>("countdown")->value(), 0);
        QCOMPARE(window.findChild<QTableWidget*>("clientsTable")->rowCount(), 0);
        QCOMPARE(window.findChild<QLabel*>("statusLabel")->text(), QString("Waiting for clients"));
    }

    void membershipUpdates() {
        lab2::host::HostController controller;
        lab2::gui::MainWindow window(controller, "fifo", 123);
        auto* table = window.findChild<QTableWidget*>("clientsTable");
        window.applyEvent({HostEventType::ClientJoined, "Connected", 0, 1, 321});
        window.applyEvent({HostEventType::ClientJoined, "Connected", 0, 2, 322});
        window.applyEvent({HostEventType::ClientJoined, "Duplicate", 0, 1, 321});
        QCOMPARE(table->rowCount(), 2);
        QCOMPARE(table->item(0, 1)->text(), QString("321"));
        QCOMPARE(table->item(0, 3)->text(), QString("Alive"));
        window.applyEvent({HostEventType::ClientLeft, "Disconnected", 0, 1, 321});
        QCOMPARE(table->rowCount(), 1);
        QCOMPARE(table->item(0, 0)->text(), QString("2"));
        QCOMPARE(window.findChild<QLabel*>("clientsCount")->text(), QString("Clients: 1"));
    }

    void resultsAndHistory() {
        lab2::host::HostController controller;
        lab2::gui::MainWindow window(controller, "fifo", 123);
        window.applyEvent({HostEventType::ClientJoined, "Connected", 0, 1, 321});
        window.applyEvent({HostEventType::RoundStarted, "Started", 1});
        lab2::game::RoundResult result;
        result.completed = true;
        result.roundNumber = 1;
        result.wolfNumber = 100;
        result.initialKidCount = 1;
        result.caughtCount = 1;
        result.deadCount = 1;
        result.consecutiveAllDeadRounds = 1;
        result.kids.push_back({1, 1, KidState::Alive, KidState::Dead, KidOutcome::Caught});
        window.applyEvent({HostEventType::RoundFinished, "Finished", 1, 0, 0, result});
        auto* table = window.findChild<QTableWidget*>("clientsTable");
        QCOMPARE(table->item(0, 2)->text(), QString("1"));
        QCOMPARE(table->item(0, 3)->text(), QString("Dead"));
        QCOMPARE(table->item(0, 4)->text(), QString("Caught"));
        auto* history = window.findChild<QTableWidget*>("historyTable");
        QCOMPARE(history->rowCount(), 1);
        QCOMPARE(history->item(0, 1)->text(), QString("100"));
        QVERIFY(window.findChild<QLabel*>("statisticsLabel")->text().contains("Dead: 1"));
        QVERIFY(
            window.findChild<QPlainTextEdit*>("eventLog")->toPlainText().contains("Client 1: 1"));
        result.roundNumber = 2;
        result.wolfNumber = 1;
        result.caughtCount = 0;
        result.deadCount = 0;
        result.aliveCount = 1;
        result.revivedCount = 1;
        result.consecutiveAllDeadRounds = 0;
        result.kids = {{1, 1, KidState::Dead, KidState::Alive, KidOutcome::Revived}};
        window.applyEvent({HostEventType::RoundFinished, "Finished", 2, 0, 0, result});
        QCOMPARE(history->rowCount(), 2);
        QCOMPARE(table->item(0, 3)->text(), QString("Alive"));
        QCOMPARE(table->item(0, 4)->text(), QString("Revived"));
    }

    void cancelledRoundNotAddedToHistory() {
        lab2::host::HostController controller;
        lab2::gui::MainWindow window(controller, "fifo", 123);
        lab2::game::RoundResult result;
        window.applyEvent({HostEventType::RoundFinished, "Cancelled", 1, 0, 0, result});
        QCOMPARE(window.findChild<QTableWidget*>("historyTable")->rowCount(), 0);
        QVERIFY(window.findChild<QLabel*>("statisticsLabel")->text().contains("cancelled"));
    }

    void gameOverSurvivesStoppedEvent() {
        lab2::host::HostController controller;
        lab2::gui::MainWindow window(controller, "fifo", 123);
        window.applyEvent({HostEventType::GameFinished, "Game over"});
        window.applyEvent({HostEventType::Stopped, "Stopped"});
        QCOMPARE(window.findChild<QLabel*>("statusLabel")->text(), QString("Game over"));
        QVERIFY(!window.findChild<QPushButton*>("stopButton")->isEnabled());
        QVERIFY(!window.findChild<QPushButton*>("submitButton")->isEnabled());
    }

    void manualAndAutomaticWolfNumbers() {
        lab2::host::HostController controller;
        lab2::gui::MainWindow window(controller, "fifo", getpid());
        auto* number = window.findChild<QSpinBox*>("wolfNumber");
        auto* submit = window.findChild<QPushButton*>("submitButton");
        auto* countdown = window.findChild<QProgressBar*>("countdown");
        auto* history = window.findChild<QTableWidget*>("historyTable");
        controller.start();
        QProcess client;
        client.start(QCoreApplication::applicationDirPath() + "/client_fifo",
                     {QString::number(static_cast<qlonglong>(getpid()))});
        QVERIFY(client.waitForStarted(3000));
        QTRY_VERIFY_WITH_TIMEOUT(submit->isEnabled(), 7000);
        QVERIFY(countdown->value() > 0);
        number->setValue(50);
        QElapsedTimer elapsed;
        elapsed.start();
        submit->click();
        QVERIFY(!submit->isEnabled());
        QTRY_VERIFY_WITH_TIMEOUT(history->rowCount() >= 1, 7000);
        QCOMPARE(history->item(0, 1)->text(), QString("50"));
        QTRY_VERIFY_WITH_TIMEOUT(history->rowCount() >= 2, 7000);
        QVERIFY(elapsed.elapsed() >= 2700);
        const int automatic = history->item(1, 1)->text().toInt();
        QVERIFY(automatic >= 1 && automatic <= 100);
        window.stopGame();
        controller.join();
        if (client.state() != QProcess::NotRunning) {
            QVERIFY(client.waitForFinished(7000));
        }
        QCOMPARE(client.exitStatus(), QProcess::NormalExit);
        QCOMPARE(client.exitCode(), 0);
    }

    void stopButtonDisablesControls() {
        lab2::host::HostController controller;
        lab2::gui::MainWindow window(controller, "fifo", 123);
        auto* stop = window.findChild<QPushButton*>("stopButton");
        stop->click();
        QVERIFY(!stop->isEnabled());
        QCOMPARE(window.findChild<QLabel*>("statusLabel")->text(), QString("Stopping..."));
        window.applyEvent({HostEventType::Stopped, "Stopped"});
        QCOMPARE(window.findChild<QLabel*>("statusLabel")->text(), QString("Host stopped"));
    }
};
} // namespace

int main(int argc, char* argv[]) {
    lab2::handshake::blockHandshakeSignals();
    QApplication app(argc, argv);
    MainWindowTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "test_main_window.moc"
