#include "Check.hpp"
#include "ChildProcess.hpp"
#include "Connection.hpp"

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <mqueue.h>
#include <sys/stat.h>
#include <sys/wait.h>

using namespace chat;

namespace {
Message receive(Connection& connection) {
    std::optional<Message> packet;
    eventually([&] { packet = connection.Read(); return packet.has_value(); }, "Packet read deadline");
    return *packet;
}
void send(Connection& connection, const Message& packet) {
    eventually([&] { return connection.Write(packet); }, "Packet write deadline");
}
void roundTrip() {
    SharedSession shared;
    auto connection = makeConnection(shared);
    const auto resource = connection->resourceName();
    const auto type = std::string(connection->type());
    const auto child = fork();
    if (child < 0) throw std::runtime_error("fork failed");
    if (child == 0) {
        try {
            connection->Open(Party::Client);
            for (unsigned i = 1; i <= 40; ++i) {
                auto request = receive(*connection);
                expect(request.sequence == i, "Per-channel ordering");
                expect(request.body() == (i == 40 ? std::string(Message::MaxText, 'z') : "ping Привет " + std::to_string(i)), "Complete payload");
                request.kind = PacketKind::Delivery;
                send(*connection, request);
            }
            connection.reset();
            _exit(0);
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; _exit(1); }
    }
    ChildProcess guard(child);
    connection->Open(Party::Host);
    for (unsigned i = 1; i <= 40; ++i) {
        auto packet = Message::compose(Party::Host, Party::Everyone, i, i,
                                      i == 40 ? std::string(Message::MaxText, 'z') : "ping Привет " + std::to_string(i));
        send(*connection, packet);
        auto answer = receive(*connection);
        expect(answer.body() == packet.body() && answer.kind == PacketKind::Delivery, "Bidirectional round trip");
    }
    int status = 0;
    eventually([&] { return waitpid(child, &status, WNOHANG) == child; }, "Child exited");
    expect(WIFEXITED(status) && WEXITSTATUS(status) == 0, "Child completed exchange");
    connection.reset();
    if (type == "fifo") expect(access(resource.c_str(), F_OK) < 0 && errno == ENOENT, "Parent removed FIFO");
    if (type == "mq") {
        const auto queue = mq_open(resource.c_str(), O_RDONLY | O_NONBLOCK);
        if (queue != -1) mq_close(queue);
        expect(queue == -1 && errno == ENOENT, "Parent unlinked MQ");
    }
}

void stalledReader() {
    SharedSession shared;
    auto connection = makeConnection(shared);
    const auto child = fork();
    if (child < 0) throw std::runtime_error("fork failed");
    if (child == 0) {
        try { connection->Open(Party::Client); sleep(15); _exit(0); }
        catch (...) { _exit(1); }
    }
    ChildProcess guard(child);
    connection->Open(Party::Host);
    const auto message = Message::compose(Party::Host, Party::Everyone, 1, 1, "pending");
    expect(connection->Write(message), "One slot available");
    expect(!connection->Write(message), "Backpressure does not overwrite unread data");
    const auto start = SharedSession::monotonicNs();
    bool failed = false;
    while (!failed && SharedSession::monotonicNs() - start < 6500000000LL) {
        try { connection->CheckProgress(); } catch (const std::exception&) { failed = true; }
        usleep(10000);
    }
    expect(failed, "Unresponsive peer fails after 5 seconds");
    expect(SharedSession::monotonicNs() - start >= 4900000000LL, "Timeout not premature");
}

void missingClient() {
    SharedSession shared;
    auto connection = makeConnection(shared);
    const auto before = SharedSession::monotonicNs();
    rejects([&] { connection->Open(Party::Host); }, "No indefinite startup wait without a client");
    const auto elapsed = SharedSession::monotonicNs() - before;
    expect(elapsed >= 4900000000LL && elapsed < 6500000000LL, "Startup timeout is 5 seconds");
}
}

int main() {
    std::signal(SIGPIPE, SIG_IGN);
    try {
        roundTrip();
        stalledReader();
        missingClient();
        std::cout << "Transport: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << "Transport FAILED: " << error.what() << '\n'; return 1; }
}
