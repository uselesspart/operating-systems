#include "client/Commands.h"

#include <QtTest>

using namespace chat;
using Kind = Command::Kind;

Q_DECLARE_METATYPE(Command::Kind)

class CommandsTest : public QObject {
    Q_OBJECT

private slots:
    void parse_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<Kind>("kind");
        QTest::addColumn<int>("to");
        QTest::addColumn<QString>("text");

        QTest::newRow("empty") << "" << Kind::Empty << kEveryone << "";
        QTest::newRow("blank") << " \t " << Kind::Empty << kEveryone << "";
        QTest::newRow("public") << "привет всем" << Kind::Say << kEveryone << "привет всем";
        QTest::newRow("public trimmed") << "  привет  \r" << Kind::Say << kEveryone << "привет";
        QTest::newRow("private") << "/to 2 привет" << Kind::Private << 2 << "привет";
        QTest::newRow("private short form") << "@3   как дела  " << Kind::Private << 3 << "как дела";
        QTest::newRow("private to host") << "/to 0 хосту" << Kind::Private << kHostId << "хосту";
        QTest::newRow("private keeps slashes") << "@2 /quit" << Kind::Private << 2 << "/quit";
        QTest::newRow("quit") << "/quit" << Kind::Quit << kEveryone << "";
        QTest::newRow("exit") << " /exit " << Kind::Quit << kEveryone << "";
        QTest::newRow("who") << "/who" << Kind::Who << kEveryone << "";
        QTest::newRow("help") << "/help" << Kind::Help << kEveryone << "";
    }

    void parse()
    {
        QFETCH(QString, line);
        QFETCH(Kind, kind);
        QFETCH(int, to);
        QFETCH(QString, text);

        const Command command = parseCommand(line.toStdString());
        QCOMPARE(command.kind, kind);
        QCOMPARE(command.to, to);
        QCOMPARE(QString::fromStdString(command.text), text);
    }

    void invalid_data()
    {
        QTest::addColumn<QString>("line");

        QTest::newRow("no recipient") << "/to";
        QTest::newRow("no text") << "/to 2";
        QTest::newRow("blank text") << "@2    ";
        QTest::newRow("not a number") << "/to боб привет";
        QTest::newRow("number with suffix") << "@2x привет";
        QTest::newRow("negative") << "@-1 привет";
        QTest::newRow("too large") << "@99999999999 привет";
        QTest::newRow("unknown command") << "/kick 2";
    }

    void invalid()
    {
        QFETCH(QString, line);
        const Command command = parseCommand(line.toStdString());
        QCOMPARE(command.kind, Kind::Invalid);
        QVERIFY(!command.text.empty());
    }
};

QTEST_GUILESS_MAIN(CommandsTest)
#include "test_commands.moc"
