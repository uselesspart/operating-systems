#include "ForkedProcess.h"
#include "Throws.h"
#include "ipc/Semaphore.h"

#include <QtTest>

#include <atomic>
#include <system_error>
#include <thread>

#include <csignal>
#include <fcntl.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>

using namespace chat;
using namespace chat::testing;

namespace {

void ignoreSignal(int) {}

}

class SemaphoreTest : public QObject {
    Q_OBJECT

private:
    int counter_ = 0;

    // Unique per test and per process, with the pid where RuntimeDir::removeStale looks for it
    std::string uniqueName()
    {
        return "/chat_" + std::to_string(::getpid()) + "_test_" + std::to_string(++counter_);
    }

private slots:
    void postThenWait()
    {
        NamedSemaphore semaphore = NamedSemaphore::create(uniqueName());
        QVERIFY(semaphore.post());
        QVERIFY(semaphore.post());
        QVERIFY(semaphore.waitFor(0ms));
        QVERIFY(semaphore.waitFor(0ms));
        QVERIFY(!semaphore.waitFor(0ms));
    }

    void waitTimesOut()
    {
        NamedSemaphore semaphore = NamedSemaphore::create(uniqueName());
        QElapsedTimer timer;
        timer.start();
        QVERIFY(!semaphore.waitFor(300ms));
        QVERIFY(timer.elapsed() >= 290);
        QVERIFY(timer.elapsed() < 1000);
    }

    void openedSemaphoreSharesTheCounter()
    {
        NamedSemaphore created = NamedSemaphore::create(uniqueName());
        NamedSemaphore opened = NamedSemaphore::open(created.name());
        QVERIFY(created.post());
        QVERIFY(opened.waitFor(0ms));
    }

    void openingMissingSemaphoreThrows()
    {
        QVERIFY(throws<std::system_error>([this] { NamedSemaphore::open(uniqueName()); }));
    }

    void creatorRemovesTheName()
    {
        const std::string name = uniqueName();
        {
            NamedSemaphore created = NamedSemaphore::create(name);
            NamedSemaphore opened = NamedSemaphore::open(name);
        }
        QVERIFY(throws<std::system_error>([&name] { NamedSemaphore::open(name); }));
    }

    void createReplacesStaleSemaphore()
    {
        const std::string name = uniqueName();
        sem_t* stale = ::sem_open(name.c_str(), O_CREAT, 0600, 5);
        QVERIFY(stale != SEM_FAILED);
        ::sem_close(stale);

        NamedSemaphore fresh = NamedSemaphore::create(name);
        QVERIFY(!fresh.waitFor(0ms));
    }

    void moveTransfersOwnership()
    {
        const std::string name = uniqueName();
        NamedSemaphore first = NamedSemaphore::create(name);
        {
            NamedSemaphore second = std::move(first);
            QCOMPARE(second.name(), name);
            QVERIFY(second.post());
            QVERIFY(second.waitFor(0ms));
            first = std::move(second);
        }
        QVERIFY(first.post());
        NamedSemaphore opened = NamedSemaphore::open(name);
        QVERIFY(opened.waitFor(0ms));
    }

    void worksBetweenProcesses()
    {
        const std::string ping = uniqueName();
        const std::string pong = uniqueName();
        ForkedProcess peer([&] {
            NamedSemaphore in = NamedSemaphore::open(ping);
            NamedSemaphore out = NamedSemaphore::open(pong);
            return in.waitFor(5s) && out.post();
        });
        NamedSemaphore toPeer = NamedSemaphore::create(ping);
        NamedSemaphore fromPeer = NamedSemaphore::create(pong);
        peer.start();

        QVERIFY(!fromPeer.waitFor(100ms));
        QVERIFY(toPeer.post());
        QVERIFY(fromPeer.waitFor(5s));
        QVERIFY(peer.succeeded());
    }

    void waitSurvivesSignalInterruption()
    {
        struct sigaction action {};
        action.sa_handler = ignoreSignal;
        struct sigaction previous {};
        ::sigaction(SIGUSR2, &action, &previous);

        NamedSemaphore semaphore = NamedSemaphore::create(uniqueName());
        const pthread_t waiter = ::pthread_self();
        std::thread helper([&] {
            std::this_thread::sleep_for(50ms);
            ::pthread_kill(waiter, SIGUSR2);
            std::this_thread::sleep_for(100ms);
            semaphore.post();
        });
        const bool posted = semaphore.waitFor(3s);
        helper.join();
        ::sigaction(SIGUSR2, &previous, nullptr);
        QVERIFY(posted);
    }
};

QTEST_GUILESS_MAIN(SemaphoreTest)
#include "test_semaphore.moc"
