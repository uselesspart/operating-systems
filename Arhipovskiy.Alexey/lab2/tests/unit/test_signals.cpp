#include "ForkedProcess.h"
#include "ipc/Signals.h"

#include <QtTest>

#include <atomic>

#include <csignal>
#include <sys/wait.h>
#include <unistd.h>

using namespace chat;
using namespace chat::testing;

namespace {

constexpr int kGivenId = 42;

// Waits for a handshake request from the parent; the signals are blocked since main
bool receiveRequest(std::chrono::milliseconds timeout)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, kHandshakeSignal);
    siginfo_t info{};
    const timespec wait{static_cast<time_t>(timeout.count() / 1000),
                        static_cast<long>(timeout.count() % 1000) * 1'000'000L};
    return ::sigtimedwait(&set, &info, &wait) == kHandshakeSignal && info.si_pid == ::getppid();
}

pid_t finishedProcess()
{
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::_exit(0);
    }
    ::waitpid(pid, nullptr, 0);
    return pid;
}

void drainPendingSignals()
{
    sigset_t set;
    sigemptyset(&set);
    for (const int signal : {kHandshakeSignal, SIGINT, SIGTERM}) {
        sigaddset(&set, signal);
    }
    const timespec zero{};
    siginfo_t info{};
    while (::sigtimedwait(&set, &info, &zero) > 0) {
    }
}

}

class SignalsTest : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { drainPendingSignals(); }

    void handshakeIsAccepted()
    {
        ForkedProcess host([] { return receiveRequest(5s) && acceptHandshake(::getppid(), kGivenId); });
        host.start();
        const HandshakeResult result = requestHandshake(host.pid(), 5s);
        QCOMPARE(result.status, HandshakeStatus::Accepted);
        QCOMPARE(result.clientId, kGivenId);
        QVERIFY(host.succeeded());
    }

    void requestIsRepeatedUntilAnswered()
    {
        ForkedProcess host([] {
            return receiveRequest(5s) && receiveRequest(5s) && acceptHandshake(::getppid(), kGivenId);
        });
        host.start();
        QElapsedTimer timer;
        timer.start();
        const HandshakeResult result = requestHandshake(host.pid(), 5s);
        QCOMPARE(result.status, HandshakeStatus::Accepted);
        QVERIFY(timer.elapsed() >= 900);
        QVERIFY(host.succeeded());
    }

    void silentHostTimesOut()
    {
        ForkedProcess host([] {
            ::pause();
            return true;
        });
        host.start();
        QElapsedTimer timer;
        timer.start();
        const HandshakeResult result = requestHandshake(host.pid(), 1500ms);
        QCOMPARE(result.status, HandshakeStatus::Timeout);
        QVERIFY(timer.elapsed() >= 1450);
        QVERIFY(timer.elapsed() < 3000);
    }

    void plainSignalIsNotAnAnswer()
    {
        ForkedProcess host([] { return receiveRequest(5s) && ::kill(::getppid(), kHandshakeSignal) == 0; });
        host.start();
        QCOMPARE(requestHandshake(host.pid(), 1500ms).status, HandshakeStatus::Timeout);
        QVERIFY(host.succeeded());
    }

    void missingHostIsReportedAtOnce()
    {
        const pid_t gone = finishedProcess();
        QElapsedTimer timer;
        timer.start();
        QCOMPARE(requestHandshake(gone, 5s).status, HandshakeStatus::NoHost);
        QVERIFY(timer.elapsed() < 1000);
        QVERIFY(!acceptHandshake(gone, kGivenId));
    }

    void listenerReportsHandshakeRequests()
    {
        std::atomic<pid_t> requestFrom{0};
        SignalListener listener([&](pid_t pid) { requestFrom = pid; }, [](int) {});
        listener.start();

        ForkedProcess client([] { return ::kill(::getppid(), kHandshakeSignal) == 0; });
        client.start();
        QVERIFY(client.succeeded());
        QTRY_COMPARE_WITH_TIMEOUT(requestFrom.load(), client.pid(), 3000);
    }

    void listenerIgnoresQueuedSignals()
    {
        std::atomic<int> requests{0};
        SignalListener listener([&](pid_t) { ++requests; }, [](int) {});
        listener.start();

        ForkedProcess other([] { return acceptHandshake(::getppid(), kGivenId); });
        other.start();
        QVERIFY(other.succeeded());
        QTest::qWait(500);
        QCOMPARE(requests.load(), 0);
    }

    void listenerReportsTermination_data()
    {
        QTest::addColumn<int>("signal");
        QTest::newRow("SIGTERM") << SIGTERM;
        QTest::newRow("SIGINT") << SIGINT;
    }

    void listenerReportsTermination()
    {
        QFETCH(int, signal);
        std::atomic<int> received{0};
        SignalListener listener([](pid_t) {}, [&](int value) { received = value; });
        listener.start();
        ::kill(::getpid(), signal);
        QTRY_COMPARE_WITH_TIMEOUT(received.load(), signal, 3000);
    }

    void listenerStopsQuickly()
    {
        SignalListener listener([](pid_t) {}, [](int) {});
        listener.start();
        QElapsedTimer timer;
        timer.start();
        listener.stop();
        QVERIFY(timer.elapsed() < 1000);
    }

    void terminationRequestIsTakenOnce()
    {
        QVERIFY(!takeTerminationRequest());
        ::kill(::getpid(), SIGTERM);
        QVERIFY(takeTerminationRequest());
        QVERIFY(!takeTerminationRequest());
    }
};

// Signals must be blocked before QtTest starts its watchdog thread
int main(int argc, char* argv[])
{
    blockProcessSignals();
    QCoreApplication app(argc, argv);
    SignalsTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_signals.moc"
