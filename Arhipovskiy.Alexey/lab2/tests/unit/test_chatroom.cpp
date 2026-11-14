#include "chat/ChatRoom.h"

#include <QtTest>

#include <algorithm>
#include <set>
#include <thread>

using namespace chat;

namespace {

constexpr int kAlice = 1;
constexpr int kBob = 2;
constexpr int kCarol = 3;

std::vector<std::string> texts(const std::vector<ChatMessage>& messages)
{
    std::vector<std::string> result;
    for (const ChatMessage& message : messages) {
        result.push_back(message.text);
    }
    return result;
}

}

class ChatRoomTest : public QObject {
    Q_OBJECT

private:
    Millis now_ = 1'000'000;

    ChatRoom makeRoom(std::chrono::milliseconds idleLimit = kDefaultIdleLimit)
    {
        return ChatRoom(idleLimit, [this] { return now_; });
    }

    void joinAll(ChatRoom& room)
    {
        room.join(kAlice, "алиса");
        room.join(kBob, "боб");
        room.join(kCarol, "кэрол");
        for (const int id : {kAlice, kBob, kCarol}) {
            room.collect(id);
        }
    }

    // Lets every queued message pass the ordering delay
    Delivery collectLater(ChatRoom& room, int id)
    {
        now_ += kOrderingDelay.count();
        return room.collect(id);
    }

    OutgoingMessage say(const std::string& text, int to = kEveryone) const { return {to, now_, text}; }

private slots:
    void namesAreSanitizedAndUnique()
    {
        ChatRoom room = makeRoom();
        QCOMPARE(room.join(kAlice, "  алиса\t"), std::string("алиса"));
        QCOMPARE(room.join(kBob, "алиса"), std::string("алиса (2)"));
        QCOMPARE(room.join(kCarol, ChatRoom::kHostName), std::string("Хост (3)"));
        QCOMPARE(room.join(4, " \n "), std::string("Клиент 4"));
        QCOMPARE(room.join(5, std::string(100, 'x')), std::string(kMaxNameBytes, 'x'));
    }

    void participantsStartWithHost()
    {
        ChatRoom room = makeRoom();
        const std::vector<Participant> hostOnly{{kHostId, ChatRoom::kHostName}};
        QVERIFY(room.participants() == hostOnly);
        room.join(kBob, "боб");
        room.join(kAlice, "алиса");
        const std::vector<Participant> expected{
            {kHostId, ChatRoom::kHostName}, {kAlice, "алиса"}, {kBob, "боб"}};
        QVERIFY(room.participants() == expected);
        QVERIFY(room.contains(kAlice));
        QVERIFY(!room.contains(kCarol));
    }

    void joinAndLeaveNotifyOthers()
    {
        ChatRoom room = makeRoom();
        const std::uint64_t initialVersion = room.rosterVersion();
        room.join(kAlice, "алиса");
        room.join(kBob, "боб");
        QCOMPARE(room.collect(kAlice).notices, std::vector<std::string>{"боб присоединился к чату"});
        QVERIFY(room.collect(kBob).notices.empty());

        room.leave(kBob, "вышел");
        QCOMPARE(room.collect(kAlice).notices, std::vector<std::string>{"боб покинул чат (вышел)"});
        QVERIFY(!room.contains(kBob));
        QCOMPARE(room.rosterVersion(), initialVersion + 3);

        room.leave(kBob, "вышел");
        QCOMPARE(room.rosterVersion(), initialVersion + 3);
    }

    void publicMessageReachesEveryone()
    {
        ChatRoom room = makeRoom();
        joinAll(room);
        const PostResult result = room.post(kAlice, say("всем привет"));
        QVERIFY(result.message.has_value());
        QVERIFY(result.visibleToHost);
        QCOMPARE(result.message->fromName, std::string("алиса"));
        QVERIFY(!result.message->isPrivate());

        for (const int id : {kAlice, kBob, kCarol}) {
            QCOMPARE(texts(collectLater(room, id).messages), std::vector<std::string>{"всем привет"});
        }
    }

    void privateMessageOnlyForSenderAndRecipient()
    {
        ChatRoom room = makeRoom();
        joinAll(room);
        const PostResult result = room.post(kAlice, say("секрет", kBob));
        QVERIFY(result.message.has_value());
        QVERIFY(!result.visibleToHost);
        QCOMPARE(result.message->toName, std::string("боб"));

        QCOMPARE(collectLater(room, kAlice).messages.size(), std::size_t{1});
        QCOMPARE(collectLater(room, kBob).messages.size(), std::size_t{1});
        QVERIFY(collectLater(room, kCarol).messages.empty());
    }

    void privateMessageToHostIsVisibleOnlyToHost()
    {
        ChatRoom room = makeRoom();
        joinAll(room);
        const PostResult result = room.post(kAlice, say("хосту", kHostId));
        QVERIFY(result.visibleToHost);
        QCOMPARE(result.message->toName, std::string(ChatRoom::kHostName));
        QCOMPARE(collectLater(room, kAlice).messages.size(), std::size_t{1});
        QVERIFY(collectLater(room, kBob).messages.empty());
    }

    void hostSendsPublicAndPrivateMessages()
    {
        ChatRoom room = makeRoom();
        joinAll(room);
        QVERIFY(room.post(kHostId, say("объявление")).message.has_value());
        const PostResult personal = room.post(kHostId, say("лично", kCarol));
        QVERIFY(personal.visibleToHost);
        QCOMPARE(personal.message->fromName, std::string(ChatRoom::kHostName));

        QCOMPARE(texts(collectLater(room, kAlice).messages), std::vector<std::string>{"объявление"});
        QCOMPARE(texts(collectLater(room, kCarol).messages),
                 (std::vector<std::string>{"объявление", "лично"}));
    }

    void rejectsInvalidMessages_data()
    {
        QTest::addColumn<int>("from");
        QTest::addColumn<int>("to");
        QTest::addColumn<QString>("text");

        QTest::newRow("blank") << kAlice << kEveryone << " \t ";
        QTest::newRow("too long") << kAlice << kEveryone << QString(kMaxTextBytes + 1, 'x');
        QTest::newRow("to self") << kAlice << kAlice << "я";
        QTest::newRow("unknown recipient") << kAlice << 42 << "кто здесь";
        QTest::newRow("unknown sender") << 42 << kEveryone << "привет";
    }

    void rejectsInvalidMessages()
    {
        QFETCH(int, from);
        QFETCH(int, to);
        QFETCH(QString, text);

        ChatRoom room = makeRoom();
        joinAll(room);
        const PostResult result = room.post(from, say(text.toStdString(), to));
        QVERIFY(!result.message.has_value());
        QVERIFY(!result.error.empty());

        for (const int id : {kAlice, kBob, kCarol}) {
            const Delivery delivery = collectLater(room, id);
            QVERIFY(delivery.messages.empty());
            const bool errorShownToSender =
                id == from && delivery.notices == std::vector<std::string>{result.error};
            QVERIFY(errorShownToSender || (id != from && delivery.notices.empty()));
        }
    }

    void textIsSanitized()
    {
        ChatRoom room = makeRoom();
        joinAll(room);
        const PostResult result = room.post(kAlice, say("\x1b[2Jпри\xd0вет\x1b[D\r\n"));
        QCOMPARE(result.message->text, std::string("привет"));
    }

    void deliveryWaitsForOrderingDelay()
    {
        ChatRoom room = makeRoom();
        joinAll(room);
        room.post(kAlice, say("раз"));
        QVERIFY(room.collect(kBob).messages.empty());
        now_ += kOrderingDelay.count() - 1;
        QVERIFY(room.collect(kBob).messages.empty());
        now_ += 1;
        QCOMPARE(room.collect(kBob).messages.size(), std::size_t{1});
        QVERIFY(room.collect(kBob).messages.empty());
    }

    void deliveryFollowsSendingTime()
    {
        ChatRoom room = makeRoom();
        joinAll(room);
        room.post(kAlice, {kEveryone, now_ - 100, "второе"});
        room.post(kBob, {kEveryone, now_ - 300, "первое"});
        room.post(kAlice, {kEveryone, now_ - 100, "третье"});
        QCOMPARE(texts(collectLater(room, kCarol).messages),
                 (std::vector<std::string>{"первое", "второе", "третье"}));
    }

    void sendTimeFromTheFutureIsReplaced()
    {
        ChatRoom room = makeRoom();
        joinAll(room);
        QCOMPARE(room.post(kAlice, {kEveryone, now_ + 60'000, "из будущего"}).message->sentAtMs, now_);
        QCOMPARE(room.post(kAlice, {kEveryone, 0, "без времени"}).message->sentAtMs, now_);
    }

    void deliveryIsBatched()
    {
        ChatRoom room = makeRoom();
        joinAll(room);
        for (std::size_t i = 0; i < kMaxBatch + 4; ++i) {
            room.post(kAlice, say(std::to_string(i)));
        }
        QCOMPARE(collectLater(room, kBob).messages.size(), kMaxBatch);
        QCOMPARE(room.collect(kBob).messages.size(), std::size_t{4});
    }

    void idleAfterLimitWithoutMessages()
    {
        ChatRoom room = makeRoom(std::chrono::seconds(2));
        room.join(kAlice, "алиса");
        now_ += 2000;
        QVERIFY(!room.isIdle(kAlice));
        now_ += 1;
        QVERIFY(room.isIdle(kAlice));

        room.post(kAlice, say("я здесь"));
        QVERIFY(!room.isIdle(kAlice));
    }

    void rejectedMessagesDoNotCountAsActivity()
    {
        ChatRoom room = makeRoom(std::chrono::seconds(2));
        room.join(kAlice, "алиса");
        now_ += 2001;
        room.post(kAlice, say("   "));
        QVERIFY(room.isIdle(kAlice));
    }

    void unknownClientIsNeverIdle()
    {
        ChatRoom room = makeRoom(std::chrono::seconds(1));
        now_ += 10'000;
        QVERIFY(!room.isIdle(kAlice));
        QVERIFY(room.collect(kAlice).messages.empty());
    }

    void concurrentPostsAreDeliveredOnce()
    {
        constexpr int kSenders = 4;
        constexpr int kPerSender = 200;
        ChatRoom room(kDefaultIdleLimit, [] { return wallClockMs(); });
        for (int id = 1; id <= kSenders + 1; ++id) {
            room.join(id, "участник");
        }
        const int reader = kSenders + 1;

        std::vector<ChatMessage> received;
        std::vector<std::thread> senders;
        for (int id = 1; id <= kSenders; ++id) {
            senders.emplace_back([&room, id] {
                for (int i = 0; i < kPerSender; ++i) {
                    room.post(id, {kEveryone, 0, std::to_string(i)});
                }
            });
        }
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (received.size() < std::size_t{kSenders * kPerSender}
               && std::chrono::steady_clock::now() < deadline) {
            const Delivery delivery = room.collect(reader);
            received.insert(received.end(), delivery.messages.begin(), delivery.messages.end());
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        for (std::thread& sender : senders) {
            sender.join();
        }

        QCOMPARE(received.size(), std::size_t{kSenders * kPerSender});
        std::set<std::uint64_t> sequence;
        for (const ChatMessage& message : received) {
            sequence.insert(message.seq);
        }
        QCOMPARE(sequence.size(), received.size());
        QVERIFY(std::is_sorted(received.begin(), received.end(), sentBefore));
    }
};

QTEST_GUILESS_MAIN(ChatRoomTest)
#include "test_chatroom.moc"
