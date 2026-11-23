#include "chat.hpp"
#include "window.hpp"

#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTest>
#include <QListWidget>
#include <stdexcept>

class ChatTests : public QObject {
    Q_OBJECT
private slots:
    void messageValidation() {
        const auto message = chatMessage(10, 20, "Привет", 1);
        validateMessage(message, 10);
        QCOMPARE(message["text"].toString(), QString("Привет"));
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, validateMessage(message, 30));
        auto invalid = message;
        invalid["text"] = "  ";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, validateMessage(invalid, 10));
        invalid = message;
        invalid["recipient"] = -1;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, validateMessage(invalid, 10));
        invalid["recipient"] = 1.5;
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, validateMessage(invalid, 10));
        invalid = message;
        invalid["time"] = "abc";
        QVERIFY_THROWS_EXCEPTION(std::runtime_error, validateMessage(invalid, 10));
    }

    void chronologicalOrder() {
        ChatWindow window(10, 20);
        auto early = chatMessage(10, 0, "Первое", 1);
        auto late = chatMessage(20, 0, "Второе", 1);
        early["time"] = "1000000";
        late["time"] = "2000000";
        window.Handle(late);
        window.Handle(early);
        auto* messages = window.findChild<QListWidget*>("messages");
        QCOMPARE(messages->count(), 2);
        QVERIFY(messages->item(0)->text().endsWith("\nПервое"));
        QVERIFY(messages->item(1)->text().endsWith("\nВторое"));
        QVERIFY(earlierMessage(early, late));
        QVERIFY(!earlierMessage(late, early));
    }

    void recipientsAndSending() {
        ChatWindow window(10, 20);
        window.Handle({{"type", "members"}, {"members", QJsonArray{10, 20, 30}}, {"text", "Подключено"}});
        auto* recipients = window.findChild<QComboBox*>("recipient");
        QCOMPARE(recipients->count(), 3);
        QCOMPARE(recipients->findData(20), -1);
        recipients->setCurrentIndex(recipients->findData(30));
        pid_t recipient = -1;
        QString sent;
        window.SetSender([&](pid_t id, const QString& text) { recipient = id; sent = text; });
        auto* input = window.findChild<QLineEdit*>("messageText");
        auto* button = window.findChild<QPushButton*>("send");
        input->setText("Личное сообщение");
        button->click();
        QCOMPARE(recipient, 30);
        QCOMPARE(sent, QString("Личное сообщение"));
        QVERIFY(input->text().isEmpty());
        window.Handle({{"type", "members"}, {"members", QJsonArray{10, 20}}, {"text", "Клиент отключился"}});
        QCOMPARE(recipients->currentData().toInt(), 0);
        QCOMPARE(recipients->findData(30), -1);
        window.Handle({{"type", "closed"}, {"text", "Соединение закрыто"}});
        QVERIFY(!input->isEnabled());
        QVERIFY(!button->isEnabled());
        QCOMPARE(window.findChild<QLabel*>("status")->text(), QString("Соединение закрыто"));
    }

    void plainText() {
        ChatWindow window(10, 20);
        window.Handle(chatMessage(10, 20, "<b>Текст</b> & \"кавычки\"", 1));
        auto* messages = window.findChild<QListWidget*>("messages");
        QVERIFY(messages->item(0)->text().endsWith("\n<b>Текст</b> & \"кавычки\""));
    }
};

QTEST_MAIN(ChatTests)
#include "unit.moc"
