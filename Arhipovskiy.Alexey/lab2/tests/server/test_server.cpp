#include "chat/ChatRoom.h"
#include "host/HostServer.h"
#include "ipc/Signals.h"

#include <QDeadlineTimer>
#include <QProcess>
#include <QtTest>

#include <algorithm>
#include <memory>
#include <mutex>

#include <csignal>
#include <unistd.h>

using namespace chat;

namespace {

constexpr int kBob = 2;
constexpr int kTimeoutMs = 5000;

// What the host's window would show, recorded from the server's threads
class RecordingListener : public HostListener {
public:
    void onStatus(const std::string& text) override
    {
        std::lock_guard lock(mutex_);
        statuses_.push_back(text);
    }

    void onParticipantsChanged(const std::vector<Participant>& participants) override
    {
        std::lock_guard lock(mutex_);
        participants_ = participants;
    }

    void onMessage(const ChatMessage& message) override
    {
        std::lock_guard lock(mutex_);
        messages_.push_back(message.text);
    }

    void onShutdownRequested() override {}

    bool hasStatus(const std::string& fragment) const
    {
        std::lock_guard lock(mutex_);
        return std::any_of(statuses_.begin(), statuses_.end(), [&fragment](const std::string& status) {
            return status.find(fragment) != std::string::npos;
        });
    }

    bool hasMessage(const std::string& text) const
    {
        std::lock_guard lock(mutex_);
        return std::find(messages_.begin(), messages_.end(), text) != messages_.end();
    }

    std::size_t participantCount() const
    {
        std::lock_guard lock(mutex_);
        return participants_.size();
    }

private:
    mutable std::mutex mutex_;
    std::vector<std::string> statuses_;
    std::vector<Participant> participants_;
    std::vector<std::string> messages_;
};

// The real client_<type> program talking to the server in this process
class ClientProcess {
public:
    explicit ClientProcess(const QString& name)
    {
        process_.start(CHAT_CLIENT_BINARY, {QString::number(::getpid()), name});
    }

    ~ClientProcess()
    {
        if (process_.state() != QProcess::NotRunning) {
            process_.kill();
            process_.waitForFinished(kTimeoutMs);
        }
    }

    void type(const QString& line)
    {
        process_.write((line + "\n").toUtf8());
        process_.waitForBytesWritten(kTimeoutMs);
    }
    void closeInput() { process_.closeWriteChannel(); }
    void kill() { process_.kill(); }

    bool waitForOutput(const QString& fragment, int timeoutMs = kTimeoutMs)
    {
        const QDeadlineTimer deadline(timeoutMs);
        while (!output().contains(fragment)) {
            if (deadline.hasExpired()
                || !process_.waitForReadyRead(static_cast<int>(deadline.remainingTime()))) {
                return output().contains(fragment);
            }
        }
        return true;
    }

    // Output gathered for a while, to check that something did not arrive
    QString outputAfter(int ms)
    {
        QTest::qWait(ms);
        return output();
    }

    bool waitForExit(int timeoutMs = kTimeoutMs) { return process_.waitForFinished(timeoutMs) || !running(); }
    bool running() const { return process_.state() != QProcess::NotRunning; }
    int exitCode() const { return process_.exitCode(); }
    bool killed() const { return process_.exitStatus() == QProcess::CrashExit; }

    // Decoded as a whole: a chunk read from the pipe may end in the middle of a UTF-8 character
    QString output()
    {
        output_ += process_.readAllStandardOutput();
        return QString::fromUtf8(output_);
    }

private:
    QProcess process_;
    QByteArray output_;
};

}

class ServerTest : public QObject {
    Q_OBJECT

private:
    std::unique_ptr<RecordingListener> listener_;
    std::unique_ptr<HostServer> server_;
    std::vector<std::unique_ptr<ClientProcess>> clients_;

    void startServer(HostOptions options = {})
    {
        listener_ = std::make_unique<RecordingListener>();
        server_ = std::make_unique<HostServer>(*listener_, options);
        server_->start();
    }

    ClientProcess& join(const QString& name)
    {
        clients_.push_back(std::make_unique<ClientProcess>(name));
        ClientProcess& client = *clients_.back();
        if (!client.waitForOutput("Подключено к хосту")) {
            qWarning("client %s did not connect: %s", qPrintable(name), qPrintable(client.output()));
        }
        return client;
    }

private slots:
    void cleanup()
    {
        server_.reset();
        clients_.clear();
        listener_.reset();
    }

    void joinAndLeaveAreReported()
    {
        startServer();
        ClientProcess& alice = join("алиса");
        QVERIFY(alice.output().contains("участник 1 «алиса»"));
        QTRY_VERIFY(listener_->hasStatus("Клиент 1 «алиса» присоединился"));
        QCOMPARE(listener_->participantCount(), std::size_t{2});

        alice.type("/quit");
        QVERIFY(alice.waitForExit());
        QCOMPARE(alice.exitCode(), 0);
        QTRY_VERIFY(listener_->hasStatus("Клиент 1 «алиса» отключён: вышел"));
        QTRY_COMPARE(listener_->participantCount(), std::size_t{1});
    }

    void clientLeavesOnEndOfInput()
    {
        startServer();
        ClientProcess& alice = join("алиса");
        alice.closeInput();
        QVERIFY(alice.waitForExit());
        QCOMPARE(alice.exitCode(), 0);
        QTRY_VERIFY(listener_->hasStatus("отключён: вышел"));
    }

    void hostSendsPublicAndPrivateMessages()
    {
        startServer();
        ClientProcess& alice = join("алиса");
        ClientProcess& bob = join("боб");

        server_->sendMessage(kEveryone, "всем привет");
        QVERIFY(listener_->hasMessage("всем привет"));
        QVERIFY(alice.waitForOutput("Хост: всем привет"));
        QVERIFY(bob.waitForOutput("Хост: всем привет"));

        server_->sendMessage(kBob, "только бобу");
        QVERIFY(bob.waitForOutput("Хост → вам (лично): только бобу"));
        QVERIFY(!alice.outputAfter(1000).contains("только бобу"));
    }

    void hostCannotWriteToUnknownParticipant()
    {
        startServer();
        server_->sendMessage(42, "кто здесь");
        QVERIFY(listener_->hasStatus("Участник 42 не найден"));
        QVERIFY(!listener_->hasMessage("кто здесь"));
    }

    void clientMessagesAreRelayed()
    {
        startServer();
        ClientProcess& alice = join("алиса");
        ClientProcess& bob = join("боб");

        alice.type("всем от алисы");
        QVERIFY(bob.waitForOutput("алиса: всем от алисы"));
        QVERIFY(alice.waitForOutput("Вы: всем от алисы"));
        QTRY_VERIFY(listener_->hasMessage("всем от алисы"));

        bob.type("/to 0 лично хосту");
        QTRY_VERIFY(listener_->hasMessage("лично хосту"));

        alice.type("@2 секрет для боба");
        QVERIFY(bob.waitForOutput("алиса → вам (лично): секрет для боба"));
        QVERIFY(listener_->hasStatus("Передано личное сообщение: алиса → боб"));
        QVERIFY(!listener_->hasMessage("секрет для боба"));
        QVERIFY(!alice.output().contains("лично хосту"));
    }

    void silentClientIsKilled()
    {
        startServer({std::chrono::seconds(2)});
        ClientProcess& silent = join("молчун");
        ClientProcess& talker = join("болтун");
        for (int i = 0; i < 8; ++i) {
            talker.type("сообщение " + QString::number(i));
            QTest::qWait(400);
        }
        QVERIFY(silent.waitForExit());
        QVERIFY(silent.killed());
        QVERIFY(talker.running());
        QVERIFY(listener_->hasStatus("Клиент 1 «молчун» отключён: не писал дольше 2 с, отправлен SIGKILL"));
        QVERIFY(talker.waitForOutput("молчун покинул чат"));
    }

    void stoppingHostSaysGoodbye()
    {
        startServer();
        ClientProcess& alice = join("алиса");
        server_->stop();
        QVERIFY(alice.waitForExit());
        QCOMPARE(alice.exitCode(), 0);
        QVERIFY(alice.output().contains("Чат закрыт: хост завершает работу"));
    }

    void killedClientIsNoticed()
    {
        startServer();
        ClientProcess& alice = join("алиса");
        ClientProcess& bob = join("боб");
        alice.kill();
        QTRY_VERIFY_WITH_TIMEOUT(listener_->hasStatus("Клиент 1 «алиса» отключён"), 8000);
        QVERIFY(bob.waitForOutput("алиса покинул чат"));
        QCOMPARE(listener_->participantCount(), std::size_t{2});
    }

    void manyClientsSeeEveryMessage()
    {
        constexpr int kClients = 10;
        startServer();
        for (int i = 1; i <= kClients; ++i) {
            join(QStringLiteral("участник%1").arg(i));
        }
        for (const auto& client : clients_) {
            client->type("привет");
        }
        for (int sender = 0; sender < kClients; ++sender) {
            for (int receiver = 0; receiver < kClients; ++receiver) {
                const QString from =
                    sender == receiver ? QStringLiteral("Вы") : QStringLiteral("участник%1").arg(sender + 1);
                ClientProcess& client = *clients_[receiver];
                QVERIFY2(client.waitForOutput(from + ": привет"), qPrintable(client.output()));
            }
        }
        QCOMPARE(listener_->participantCount(), std::size_t{kClients + 1});
    }
};

// Signals must be blocked before any thread starts, QtTest's watchdog included
int main(int argc, char* argv[])
{
    blockProcessSignals();
    std::signal(SIGPIPE, SIG_IGN);
    QCoreApplication app(argc, argv);
    ServerTest test;
    return QTest::qExec(&test, argc, argv);
}

#include "test_server.moc"
