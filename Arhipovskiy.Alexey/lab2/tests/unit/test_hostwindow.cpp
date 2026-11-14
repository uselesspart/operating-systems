#include "gui/HostWindow.h"

#include <QComboBox>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QtTest>

#include <thread>
#include <utility>
#include <vector>

using namespace chat;

namespace {

class RecordingController : public HostController {
public:
    void sendMessage(int to, const std::string& text) override { sent.emplace_back(to, text); }

    std::vector<std::pair<int, std::string>> sent;
};

ChatMessage message(std::uint64_t seq, Millis sentAtMs, const std::string& text, int to = kEveryone)
{
    return {seq, sentAtMs, 1, to, "алиса", to == kEveryone ? "" : "боб", text};
}

const std::vector<Participant> kRoster{{kHostId, "Хост"}, {1, "алиса"}, {2, "боб"}};

}

class HostWindowTest : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<HostWindow> window_;
    RecordingController controller_;

    template <typename Widget>
    Widget* find(const char* name)
    {
        return window_->findChild<Widget*>(name);
    }

    QListWidget* chat() { return find<QListWidget>("chat"); }
    QListWidget* participants() { return find<QListWidget>("participants"); }
    QComboBox* recipient() { return find<QComboBox>("recipient"); }
    QLineEdit* input() { return find<QLineEdit>("input"); }
    QString events() { return find<QPlainTextEdit>("events")->toPlainText(); }

    void selectRecipient(int id) { recipient()->setCurrentIndex(recipient()->findData(id)); }

private slots:
    void init()
    {
        controller_.sent.clear();
        window_ = std::make_unique<HostWindow>("sock", 4242);
        window_->setController(&controller_);
        window_->show();
    }

    void cleanup() { window_.reset(); }

    void titleNamesTypeAndPid()
    {
        QVERIFY(window_->windowTitle().contains("sock"));
        QVERIFY(window_->windowTitle().contains("4242"));
    }

    void enterSendsToEveryone()
    {
        input()->setText("  всем привет ");
        QTest::keyClick(input(), Qt::Key_Return);
        QCOMPARE(controller_.sent.size(), std::size_t{1});
        QCOMPARE(controller_.sent[0].first, kEveryone);
        QCOMPARE(controller_.sent[0].second, std::string("всем привет"));
        QVERIFY(input()->text().isEmpty());
    }

    void buttonSendsPrivateToSelectedParticipant()
    {
        window_->onParticipantsChanged(kRoster);
        QTRY_COMPARE(recipient()->count(), 3);
        selectRecipient(2);
        input()->setText("лично");
        QTest::mouseClick(find<QPushButton>("send"), Qt::LeftButton);
        QCOMPARE(controller_.sent.size(), std::size_t{1});
        QCOMPARE(controller_.sent[0].first, 2);
    }

    void blankInputIsNotSent()
    {
        input()->setText("   ");
        QTest::keyClick(input(), Qt::Key_Return);
        QVERIFY(controller_.sent.empty());
    }

    void participantsListMarksHost()
    {
        window_->onParticipantsChanged(kRoster);
        QTRY_COMPARE(participants()->count(), 3);
        QVERIFY(participants()->item(0)->text().contains("(вы)"));
        QVERIFY(participants()->item(2)->text().contains("боб"));
        QCOMPARE(recipient()->itemData(0).toInt(), kEveryone);
    }

    void selectionSurvivesRosterChanges()
    {
        window_->onParticipantsChanged(kRoster);
        QTRY_COMPARE(recipient()->count(), 3);
        selectRecipient(2);

        window_->onParticipantsChanged({{kHostId, "Хост"}, {2, "боб"}, {3, "кэрол"}});
        QTRY_VERIFY(recipient()->findData(3) >= 0);
        QCOMPARE(recipient()->currentData().toInt(), 2);

        window_->onParticipantsChanged({{kHostId, "Хост"}, {3, "кэрол"}});
        QTRY_COMPARE(recipient()->count(), 2);
        QCOMPARE(recipient()->currentData().toInt(), kEveryone);
    }

    void statusGoesToEventLog()
    {
        window_->onStatus("Клиент 1 «алиса» присоединился");
        QTRY_VERIFY(events().contains("Клиент 1 «алиса» присоединился"));
    }

    void messagesAreOrderedBySendingTime()
    {
        window_->onMessage(message(1, 2000, "третье"));
        window_->onMessage(message(2, 1000, "первое"));
        window_->onMessage(message(3, 1000, "второе"));
        window_->onMessage(message(4, 3000, "четвёртое"));
        QTRY_COMPARE(chat()->count(), 4);

        const QStringList expected{"первое", "второе", "третье", "четвёртое"};
        for (int row = 0; row < expected.size(); ++row) {
            QVERIFY2(chat()->item(row)->text().endsWith(expected[row]),
                     qPrintable(chat()->item(row)->text()));
        }
    }

    void privateMessagesAreMarked()
    {
        window_->onMessage(message(1, 1000, "секрет", kHostId));
        QTRY_COMPARE(chat()->count(), 1);
        QVERIFY(chat()->item(0)->text().contains("(лично)"));
        QVERIFY(chat()->item(0)->font().italic());
    }

    void callbacksFromOtherThreadsAreQueued()
    {
        constexpr int kThreads = 4;
        constexpr int kPerThread = 25;
        std::vector<std::thread> threads;
        for (int t = 0; t < kThreads; ++t) {
            threads.emplace_back([this, t] {
                for (int i = 0; i < kPerThread; ++i) {
                    const auto seq = static_cast<std::uint64_t>(t * kPerThread + i);
                    window_->onMessage(message(seq, 1000 + static_cast<Millis>(seq), std::to_string(seq)));
                    window_->onStatus("событие");
                }
            });
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
        QTRY_COMPARE(chat()->count(), kThreads * kPerThread);
        for (int row = 0; row < chat()->count(); ++row) {
            QVERIFY(chat()->item(row)->text().endsWith(": " + QString::number(row)));
        }
    }

    void shutdownRequestClosesWindow()
    {
        QVERIFY(window_->isVisible());
        window_->onShutdownRequested();
        QTRY_VERIFY(!window_->isVisible());
    }
};

QTEST_MAIN(HostWindowTest)
#include "test_hostwindow.moc"
