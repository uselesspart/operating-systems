#include "Throws.h"
#include "chat/Limits.h"
#include "chat/Protocol.h"
#include "ipc/Channel.h"

#include <QtTest>

using namespace chat;
using namespace chat::protocol;
using chat::testing::throws;

namespace {

constexpr Millis kSomeTime = 1'700'000'000'000;

ChatMessage sampleMessage(std::uint64_t seq)
{
    const Millis sentAt = kSomeTime + static_cast<Millis>(seq);
    return {seq, sentAt, 1, 2, "алиса", "боб", "привет, " + std::to_string(seq)};
}

bool sameMessage(const ChatMessage& a, const ChatMessage& b)
{
    return a.seq == b.seq && a.sentAtMs == b.sentAtMs && a.from == b.from && a.to == b.to
           && a.fromName == b.fromName && a.toName == b.toName && a.text == b.text;
}

}

class ProtocolTest : public QObject {
    Q_OBJECT

private slots:
    void requestRoundTrip()
    {
        Request request;
        request.kind = RequestKind::Join;
        request.name = "Алиса";
        request.outgoing = {{kEveryone, 123, "всем"}, {7, 456, "лично"}};

        const Request decoded = decodeRequest(encode(request));
        QCOMPARE(decoded.kind, RequestKind::Join);
        QCOMPARE(decoded.name, request.name);
        QCOMPARE(decoded.outgoing.size(), std::size_t{2});
        QCOMPARE(decoded.outgoing[1].to, 7);
        QCOMPARE(decoded.outgoing[1].sentAtMs, Millis{456});
        QCOMPARE(decoded.outgoing[1].text, std::string("лично"));
    }

    void responseRoundTrip()
    {
        Response response;
        response.kind = ResponseKind::Bye;
        response.clientId = 3;
        response.reason = "хост завершает работу";
        response.participants = std::vector<Participant>{{0, "Хост"}, {3, "боб"}};
        response.messages = {sampleMessage(1), sampleMessage(2)};
        response.notices = {"алиса присоединилась"};

        const Response decoded = decodeResponse(encode(response));
        QCOMPARE(decoded.kind, ResponseKind::Bye);
        QCOMPARE(decoded.clientId, 3);
        QCOMPARE(decoded.reason, response.reason);
        QVERIFY(decoded.participants.has_value());
        QVERIFY(*decoded.participants == *response.participants);
        QCOMPARE(decoded.messages.size(), std::size_t{2});
        QVERIFY(sameMessage(decoded.messages[1], response.messages[1]));
        QCOMPARE(decoded.notices, response.notices);
    }

    void participantsAreOptional()
    {
        const Response decoded = decodeResponse(encode(Response{}));
        QVERIFY(!decoded.participants.has_value());
        QVERIFY(decoded.messages.empty());
    }

    void rejectsEveryTruncation()
    {
        Response response;
        response.participants = std::vector<Participant>{{0, "Хост"}};
        response.messages = {sampleMessage(1)};
        const Bytes full = encode(response);
        for (std::size_t size = 0; size < full.size(); ++size) {
            const Bytes truncated(full.begin(), full.begin() + static_cast<std::ptrdiff_t>(size));
            QVERIFY(throws<ProtocolError>([&] { decodeResponse(truncated); }));
        }
    }

    void rejectsTrailingBytes()
    {
        Bytes bytes = encode(Request{});
        bytes.push_back(0);
        QVERIFY(throws<ProtocolError>([&] { decodeRequest(bytes); }));
    }

    void rejectsUnknownKind()
    {
        Bytes bytes = encode(Request{});
        bytes[0] = 99;
        QVERIFY(throws<ProtocolError>([&] { decodeRequest(bytes); }));
    }

    void rejectsHugeCountWithoutAllocating()
    {
        Bytes bytes = encode(Request{});
        std::fill(bytes.end() - 4, bytes.end(), 0xFF);
        QVERIFY(throws<ProtocolError>([&] { decodeRequest(bytes); }));
    }

    void largestResponseFitsIntoFrame()
    {
        Response response;
        response.participants =
            std::vector<Participant>(kMaxParticipants, {999, std::string(kMaxNameBytes, 'n')});
        for (std::size_t i = 0; i < kMaxBatch; ++i) {
            ChatMessage message = sampleMessage(i);
            message.fromName.assign(kMaxNameBytes, 'f');
            message.toName.assign(kMaxNameBytes, 't');
            message.text.assign(kMaxTextBytes, 'x');
            response.messages.push_back(message);
        }
        QVERIFY(encode(response).size() <= Channel::kMaxFrameSize);
    }

    void largestRequestFitsIntoFrame()
    {
        Request request;
        request.name.assign(kMaxNameBytes, 'n');
        request.outgoing.assign(kMaxBatch, {1, 1, std::string(kMaxTextBytes, 'x')});
        QVERIFY(encode(request).size() <= Channel::kMaxFrameSize);
    }
};

QTEST_GUILESS_MAIN(ProtocolTest)
#include "test_protocol.moc"
