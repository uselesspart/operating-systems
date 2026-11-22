#include "chat/Limits.h"
#include "chat/Text.h"

#include <QtTest>

using namespace chat;
using namespace chat::text;

namespace {

constexpr Millis kNoon = 1'700'000'000'000;

ChatMessage message(int from, int to, const std::string& text)
{
    return {1, kNoon, from, to, "алиса", to == kEveryone ? "" : "боб", text};
}

// Everything after the "[hh:mm:ss] " prefix, which depends on the local time zone
std::string withoutTime(const std::string& line)
{
    const std::string prefix = "[" + formatTime(kNoon) + "] ";
    return line.rfind(prefix, 0) == 0 ? line.substr(prefix.size()) : "no time prefix: " + line;
}

bool validUtf8(const std::string& text)
{
    return QString::fromUtf8(text.data(), static_cast<int>(text.size())).toUtf8().toStdString() == text;
}

}

class TextTest : public QObject {
    Q_OBJECT

private slots:
    void sanitizeText_data()
    {
        QTest::addColumn<QByteArray>("input");
        QTest::addColumn<QString>("expected");

        QTest::newRow("plain") << QByteArray("привет") << "привет";
        QTest::newRow("surrounding spaces") << QByteArray("  привет, мир  ") << "привет, мир";
        QTest::newRow("tabs and newlines") << QByteArray("a\tb\r\nc") << "a b  c";
        QTest::newRow("delete character") << QByteArray("a\x7f!") << "a !";
        QTest::newRow("only control characters") << QByteArray("\x01\x02\t") << "";
        QTest::newRow("empty") << QByteArray() << "";
        QTest::newRow("emoji") << QByteArray("ok 😀") << "ok 😀";

        QTest::newRow("screen control") << QByteArray("\x1b[2Jтекст") << "текст";
        QTest::newRow("colours") << QByteArray("\x1b[1;31mкрасный\x1b[0m") << "красный";
        QTest::newRow("arrow keys") << QByteArray("привет\x1b[D\x1b[Dмир") << "приветмир";
        QTest::newRow("application arrow keys") << QByteArray("a\x1bOA!") << "a!";
        QTest::newRow("lone escape") << QByteArray("a\x1b") << "a";
        QTest::newRow("C1 control") << QByteArray("a\xc2\x9b!") << "a !";

        QTest::newRow("letter half erased by Backspace") << QByteArray("даро\xd0\xd0\xb2а") << "дарова";
        QTest::newRow("stray continuation byte") << QByteArray("\xb1!") << "!";
        QTest::newRow("cut at the end") << QByteArray("abc\xd1") << "abc";
        QTest::newRow("invalid bytes") << QByteArray("\xff\xfe!") << "!";
        QTest::newRow("overlong encoding") << QByteArray("\xc0\xaf!") << "!";
        QTest::newRow("surrogate") << QByteArray("\xed\xa0\x80!") << "!";
        QTest::newRow("above U+10FFFF") << QByteArray("\xf4\x90\x80\x80!") << "!";
    }

    void sanitizeText()
    {
        QFETCH(QByteArray, input);
        QFETCH(QString, expected);
        QCOMPARE(QString::fromStdString(text::sanitizeText(input.toStdString())), expected);
    }

    void truncateKeepsWholeCharacters_data()
    {
        QTest::addColumn<QString>("input");
        QTest::addColumn<int>("maxBytes");
        QTest::addColumn<QString>("expected");

        QTest::newRow("short enough") << "abc" << 3 << "abc";
        QTest::newRow("ascii") << "abcdef" << 4 << "abcd";
        QTest::newRow("two-byte boundary") << "яяя" << 4 << "яя";
        QTest::newRow("inside two-byte") << "яяя" << 5 << "яя";
        QTest::newRow("inside three-byte") << "€€" << 4 << "€";
        QTest::newRow("inside four-byte") << "a😀" << 4 << "a";
        QTest::newRow("nothing fits") << "я" << 1 << "";
    }

    void truncateKeepsWholeCharacters()
    {
        QFETCH(QString, input);
        QFETCH(int, maxBytes);
        QFETCH(QString, expected);

        const std::string result = truncateUtf8(input.toStdString(), static_cast<std::size_t>(maxBytes));
        QCOMPARE(QString::fromStdString(result), expected);
        QVERIFY(result.size() <= static_cast<std::size_t>(maxBytes));
    }

    void nameIsCutWithoutSplittingCharacters()
    {
        std::string name = "a";
        for (int i = 0; i < 20; ++i) {
            name += "я";
        }
        const std::string cut = sanitizeName(name);
        QCOMPARE(cut.size(), kMaxNameBytes - 1);
        QVERIFY(validUtf8(cut));
    }

    void spacesLeftByCutAreTrimmed()
    {
        QCOMPARE(sanitizeName("a" + std::string(kMaxNameBytes, ' ') + "b"), std::string("a"));
    }

    void formatMessage_data()
    {
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::addColumn<int>("selfId");
        QTest::addColumn<QString>("expected");

        QTest::newRow("public from other") << 1 << kEveryone << 2 << "алиса: привет";
        QTest::newRow("public from self") << 1 << kEveryone << 1 << "Вы: привет";
        QTest::newRow("private to self") << 1 << 2 << 2 << "алиса → вам (лично): привет";
        QTest::newRow("private from self") << 1 << 2 << 1 << "Вы → боб (лично): привет";
        QTest::newRow("private seen by host") << 1 << 2 << kHostId << "алиса → боб (лично): привет";
    }

    void formatMessage()
    {
        QFETCH(int, from);
        QFETCH(int, to);
        QFETCH(int, selfId);
        QFETCH(QString, expected);

        const std::string line = text::formatMessage(message(from, to, "привет"), selfId);
        QCOMPARE(QString::fromStdString(withoutTime(line)), expected);
    }
};

QTEST_GUILESS_MAIN(TextTest)
#include "test_text.moc"
