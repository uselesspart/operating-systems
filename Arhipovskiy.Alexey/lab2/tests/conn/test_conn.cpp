#include "ForkedProcess.h"
#include "Throws.h"
#include "conn/conn.h"
#include "ipc/Channel.h"
#include "ipc/Names.h"
#include "ipc/RuntimeDir.h"
#include "ipc/Timeouts.h"

#include <QtTest>

#include <cstring>
#include <memory>
#include <numeric>
#include <system_error>

#include <csignal>
#include <dirent.h>
#include <unistd.h>

using namespace chat;
using namespace chat::testing;

namespace {

constexpr auto kSlack = 2s;

Bytes pattern(std::size_t size, std::uint8_t seed)
{
    Bytes bytes(size);
    std::iota(bytes.begin(), bytes.end(), seed);
    return bytes;
}

std::size_t countEntries(const std::string& dir)
{
    std::size_t count = 0;
    if (DIR* handle = ::opendir(dir.c_str())) {
        while (const dirent* entry = ::readdir(handle)) {
            count += std::strcmp(entry->d_name, ".") != 0 && std::strcmp(entry->d_name, "..") != 0;
        }
        ::closedir(handle);
    }
    return count;
}

std::size_t openDescriptors()
{
    return countEntries("/proc/self/fd");
}

bool readEquals(Conn& conn, const Bytes& expected)
{
    Bytes actual(expected.size());
    return conn.Read(actual.data(), actual.size()) && actual == expected;
}

// Client body: sends one byte and stays connected until the host acknowledges it
bool sendAndAwaitAck(const ConnId& id)
{
    Conn conn(id, false);
    char ack = 0;
    return conn.Write("x", 1) && conn.Read(&ack, 1);
}

bool receiveAndAck(Conn& host)
{
    char byte = 0;
    return host.Read(&byte, 1) && byte == 'x' && host.Write("a", 1);
}

}

// The same tests run against every conn_<type>.cpp: the host side lives in this process,
// the client side in a forked one, as with the real host_<type> and client_<type>
class ConnTest : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<RuntimeDir> runtimeDir_;
    int nextClientId_ = 1;

    ConnId newId() { return {::getpid(), nextClientId_++}; }

private slots:
    void initTestCase()
    {
        std::signal(SIGPIPE, SIG_IGN);
        runtimeDir_ = std::make_unique<RuntimeDir>(::getpid());
    }

    void cleanupTestCase() { runtimeDir_.reset(); }

    void typeCodeMatchesFileName() { QCOMPARE(QString(Conn::typeCode()), QString(CHAT_CONN_TYPE)); }

    void exchangeInBothDirections()
    {
        const ConnId id = newId();
        ForkedProcess client([id] {
            Conn conn(id, false);
            return conn.Write("ping", 4) && readEquals(conn, Bytes{'p', 'o', 'n', 'g'});
        });
        Conn host(id, true);
        client.start();

        QVERIFY(readEquals(host, Bytes{'p', 'i', 'n', 'g'}));
        QVERIFY(host.Write("pong", 4));
        QVERIFY(client.succeeded());
    }

    void largeTransfersArriveIntact()
    {
        constexpr std::size_t kSize = 1 << 20;
        const ConnId id = newId();
        ForkedProcess client([id] {
            Conn conn(id, false);
            const Bytes data = pattern(kSize, 1);
            return conn.Write(data.data(), data.size()) && readEquals(conn, pattern(kSize, 7));
        });
        Conn host(id, true);
        client.start();

        QVERIFY(readEquals(host, pattern(kSize, 1)));
        const Bytes reply = pattern(kSize, 7);
        QVERIFY(host.Write(reply.data(), reply.size()));
        QVERIFY(client.succeeded());
    }

    void readFailsWhenClientExits()
    {
        const ConnId id = newId();
        ForkedProcess client([id] { return sendAndAwaitAck(id); });
        Conn host(id, true);
        client.start();

        QVERIFY(receiveAndAck(host));
        QVERIFY(client.succeeded());
        char byte = 0;
        QVERIFY(!host.Read(&byte, 1));
    }

    void writeFailsWhenClientExits()
    {
        const ConnId id = newId();
        ForkedProcess client([id] { return sendAndAwaitAck(id); });
        Conn host(id, true);
        client.start();

        QVERIFY(receiveAndAck(host));
        QVERIFY(client.succeeded());
        const Bytes data = pattern(256 * 1024, 0);
        QVERIFY(!host.Write(data.data(), data.size()));
    }

    void hostGivesUpWithoutClient()
    {
        Conn host(newId(), true);
        QElapsedTimer timer;
        timer.start();
        char byte = 0;
        QVERIFY(!host.Read(&byte, 1));
        QVERIFY(timer.elapsed() >= 4900);
        QVERIFY(timer.elapsed() < std::chrono::milliseconds(kIpcTimeout + kSlack).count());
    }

    void clientCannotAttachWithoutHost()
    {
        QVERIFY(throws<std::system_error>([] { Conn(ConnId{::getpid(), 999}, false); }));
    }

    void nothingIsLeftBehind()
    {
        const std::size_t descriptorsBefore = openDescriptors();
        {
            const ConnId id = newId();
            ForkedProcess client([id] { return sendAndAwaitAck(id); });
            Conn host(id, true);
            client.start();
            QVERIFY(receiveAndAck(host));
            QVERIFY(client.succeeded());
        }
        QCOMPARE(countEntries(runtimeDir_->path()), std::size_t{0});
        QCOMPARE(openDescriptors(), descriptorsBefore);
    }

    void channelExchangesFrames()
    {
        const ConnId id = newId();
        ForkedProcess client([id] {
            Channel channel(id, Channel::Side::Client);
            Bytes reply;
            return channel.send(pattern(Channel::kMaxFrameSize, 3)) == ChannelStatus::Ok
                   && channel.receive(reply, 5s) == ChannelStatus::Ok && reply.empty();
        });
        Channel host(id, Channel::Side::Host);
        client.start();

        Bytes frame;
        QCOMPARE(host.receive(frame, 5s), ChannelStatus::Ok);
        QVERIFY(frame == pattern(Channel::kMaxFrameSize, 3));
        QCOMPARE(host.send(Bytes{}), ChannelStatus::Ok);
        QVERIFY(client.succeeded());
    }

    void channelReceiveTimesOut()
    {
        Channel host(newId(), Channel::Side::Host);
        QElapsedTimer timer;
        timer.start();
        Bytes frame;
        QCOMPARE(host.receive(frame, 200ms), ChannelStatus::Timeout);
        QVERIFY(timer.elapsed() >= 190);
        QVERIFY(timer.elapsed() < 1000);
    }

    void channelRejectsOversizedFrame()
    {
        Channel host(newId(), Channel::Side::Host);
        QCOMPARE(host.send(Bytes(Channel::kMaxFrameSize + 1)), ChannelStatus::Corrupted);
    }

    void channelDetectsGarbage_data()
    {
        QTest::addColumn<QByteArray>("header");
        QTest::newRow("wrong magic") << QByteArray("JUNK\0\0\0\0", 8);
        QTest::newRow("oversized frame") << QByteArray("CHAT\xff\xff\xff\x7f", 8);
    }

    void channelDetectsGarbage()
    {
        QFETCH(QByteArray, header);
        const ConnId id = newId();
        ForkedProcess client([id, header] {
            NamedSemaphore toHost = NamedSemaphore::open(names::semaphoreName(id, names::kToHost));
            NamedSemaphore toClient = NamedSemaphore::open(names::semaphoreName(id, names::kToClient));
            Conn conn(id, false);
            return conn.Write(header.constData(), static_cast<std::size_t>(header.size())) && toHost.post()
                   && toClient.waitFor(5s);
        });
        Channel host(id, Channel::Side::Host);
        client.start();

        Bytes frame;
        QCOMPARE(host.receive(frame, 5s), ChannelStatus::Corrupted);
        QVERIFY(NamedSemaphore::open(names::semaphoreName(id, names::kToClient)).post());
        QVERIFY(client.succeeded());
    }
};

QTEST_GUILESS_MAIN(ConnTest)
#include "test_conn.moc"
