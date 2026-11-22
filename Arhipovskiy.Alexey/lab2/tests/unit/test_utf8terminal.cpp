#include "client/Utf8Terminal.h"
#include "ipc/FdIo.h"
#include "ipc/UniqueFd.h"

#include <QtTest>

#include <cstdlib>
#include <string>

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

using namespace chat;
using namespace std::chrono_literals;

// The terminal side is the slave end of a pseudo-terminal, the test types into the master end
class Utf8TerminalTest : public QObject {
    Q_OBJECT

private:
    UniqueFd master_;
    UniqueFd slave_;

    bool hasIutf8() const
    {
        termios settings{};
        return ::tcgetattr(slave_.get(), &settings) == 0 && (settings.c_iflag & IUTF8) != 0;
    }

    void setIutf8(bool enabled)
    {
        termios settings{};
        QCOMPARE(::tcgetattr(slave_.get(), &settings), 0);
        settings.c_iflag = enabled ? (settings.c_iflag | IUTF8) : (settings.c_iflag & ~IUTF8);
        QCOMPARE(::tcsetattr(slave_.get(), TCSANOW, &settings), 0);
    }

    // Keys pressed in the terminal, then the line the program reads
    std::string typeLine(const std::string& keys)
    {
        if (::write(master_.get(), keys.data(), keys.size()) != static_cast<ssize_t>(keys.size())
            || !fdio::waitReadable(slave_.get(), 2s)) {
            return "no line";
        }
        char buffer[256];
        const ssize_t n = ::read(slave_.get(), buffer, sizeof(buffer));
        return n > 0 ? std::string(buffer, static_cast<std::size_t>(n)) : "no line";
    }

private slots:
    void init()
    {
        master_.reset(::posix_openpt(O_RDWR | O_NOCTTY | O_CLOEXEC));
        QVERIFY(master_.valid());
        QCOMPARE(::grantpt(master_.get()), 0);
        QCOMPARE(::unlockpt(master_.get()), 0);
        slave_.reset(::open(::ptsname(master_.get()), O_RDWR | O_NOCTTY | O_CLOEXEC));
        QVERIFY(slave_.valid());
        setIutf8(false);
    }

    void cleanup()
    {
        slave_.reset();
        master_.reset();
    }

    void withoutItBackspaceLeavesHalfOfLetter()
    {
        QCOMPARE(typeLine("дароб\x7fва\n"), std::string("даро\xd0ва\n"));
    }

    void backspaceErasesWholeLetter()
    {
        const Utf8Terminal terminal(slave_.get());
        QCOMPARE(typeLine("дароб\x7fва\n"), std::string("дарова\n"));
    }

    void settingsAreRestored()
    {
        {
            const Utf8Terminal terminal(slave_.get());
            QVERIFY(hasIutf8());
        }
        QVERIFY(!hasIutf8());
    }

    void enabledFlagIsKept()
    {
        setIutf8(true);
        {
            const Utf8Terminal terminal(slave_.get());
        }
        QVERIFY(hasIutf8());
    }

    void notTerminalIsIgnored()
    {
        int fds[2];
        QCOMPARE(::pipe2(fds, O_CLOEXEC), 0);
        const UniqueFd readEnd(fds[0]);
        const UniqueFd writeEnd(fds[1]);
        const Utf8Terminal terminal(readEnd.get());
        QCOMPARE(::write(writeEnd.get(), "x", 1), ssize_t{1});
        char byte = 0;
        QCOMPARE(::read(readEnd.get(), &byte, 1), ssize_t{1});
    }
};

QTEST_GUILESS_MAIN(Utf8TerminalTest)
#include "test_utf8terminal.moc"
