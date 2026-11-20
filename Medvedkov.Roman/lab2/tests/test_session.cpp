#include "Check.hpp"
#include "ChatSession.hpp"
#include "ChildProcess.hpp"

#include <csignal>
#include <sys/wait.h>
#include <vector>

using namespace chat;

namespace {
Message awaitPacket(Connection& connection) {
    std::optional<Message> message;
    eventually([&] { message = connection.Read(); return message.has_value(); }, "Await ordered delivery");
    return *message;
}

void delayedEarlierMessage() {
    SharedSession shared;
    auto connection = makeConnection(shared);
    const auto child = fork();
    if (child < 0) throw std::runtime_error("fork");
    if (child == 0) {
        try {
            connection->Open(Party::Client);
            const auto stamp = shared.reserve(Party::Client);
            shared.post(SharedSession::Gate::Created);
            usleep(150000); // Host submits a newer message before this one arrives.
            const auto first = Message::compose(Party::Client, Party::Everyone, stamp.sequence, stamp.sentAtNs, "earlier");
            eventually([&] { return connection->Write(first); }, "Send delayed request");
            const auto a = awaitPacket(*connection);
            const auto b = awaitPacket(*connection);
            expect(a.body() == "earlier" && b.body() == "later", "Client sees chronological order");
            const auto lastStamp = shared.reserve(Party::Client);
            const auto confirmed = Message::compose(Party::Client, Party::Host, lastStamp.sequence, lastStamp.sentAtNs, "confirmed");
            eventually([&] { return connection->Write(confirmed); }, "Confirm client checked both deliveries");
            awaitPacket(*connection);
            _exit(0);
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; _exit(1); }
    }
    ChildProcess guard(child);
    connection->Open(Party::Host);
    shared.wait(SharedSession::Gate::Created);
    ChatSession session(*connection, shared, Party::Host, child);
    std::vector<Message> visible;
    session.onMessage = [&](const Message& message) { visible.push_back(message); };
    session.submit("later", Party::Everyone);
    expect(visible.empty(), "Host waits for earlier in-flight message");
    eventually([&] { session.tick(); return visible.size() == 3; }, "Host receives ordered messages and client confirmation");
    expect(visible[0].body() == "earlier" && visible[1].body() == "later", "Host total order");
    expect(visible[2].body() == "confirmed" && visible[2].recipient == Party::Host, "Private request routed");
    expect(visible[0].sentAtNs < visible[1].sentAtNs, "Timestamp order agrees with sequence");
    eventually([&] { session.tick(); return !session.connected(); }, "Peer exit detected without a zombie");
}

void replyTimeout() {
    SharedSession shared;
    auto connection = makeConnection(shared);
    const auto parent = getpid();
    const auto child = fork();
    if (child < 0) throw std::runtime_error("fork");
    if (child == 0) {
        try {
            connection->Open(Party::Client);
            ChatSession session(*connection, shared, Party::Client, parent);
            session.submit("no reply", Party::Host);
            const auto start = SharedSession::monotonicNs();
            while (session.connected() && SharedSession::monotonicNs() - start < 6500000000LL) {
                session.tick(); usleep(10000);
            }
            _exit(!session.connected() && SharedSession::monotonicNs() - start >= 4900000000LL ? 0 : 1);
        } catch (...) { _exit(1); }
    }
    ChildProcess guard(child);
    connection->Open(Party::Host);
    // Intentionally leave the request unread: the client must stop, not wait forever.
    int status = 0;
    eventually([&] { return waitpid(child, &status, WNOHANG) == child; }, "Client stops on timeout");
    expect(WIFEXITED(status) && WEXITSTATUS(status) == 0, "Client session enforces 5-second deadline");
}
}

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    try {
        delayedEarlierMessage();
        replyTimeout();
        std::cout << "Session: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "Session FAILED: " << error.what() << '\n'; return 1; }
}
