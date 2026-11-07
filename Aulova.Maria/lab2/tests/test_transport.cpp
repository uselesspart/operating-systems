#include <fcntl.h>
#include <mqueue.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <string>

#include "check.h"
#include "connection.h"

#ifndef TRANSPORT_CODE
#error "TRANSPORT_CODE задаётся в CMakeLists.txt"
#endif

namespace {

using Clock = std::chrono::steady_clock;

const std::string kTransport = TRANSPORT_CODE;

long elapsedMs(Clock::time_point since)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count();
}

template <class Body>
pid_t spawn(Body body)
{
    const pid_t pid = fork();
    if (pid == 0)
        _exit(body());
    return pid;
}

int waitFor(pid_t pid)
{
    int status = 0;
    if (waitpid(pid, &status, 0) != pid)
        return -1;
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

RecvResult receiveWithin(Connection& connection, WireMessage& message, int timeoutMs)
{
    pollfd descriptor{connection.readFd(), POLLIN, 0};
    poll(&descriptor, 1, timeoutMs);
    return connection.receive(message);
}

bool receivedText(Connection& connection, const std::string& expected, int timeoutMs = 2000)
{
    WireMessage message;
    return receiveWithin(connection, message, timeoutMs) == RecvResult::Message && textOf(message) == expected;
}

SendResult say(Connection& connection, Party from, Party to, const std::string& text)
{
    return connection.send(*makeText(from, to, text));
}

std::string fifoPath(pid_t owner, const char* direction)
{
    return "/tmp/os_lab2_" + std::to_string(owner) + "_" + direction;
}

bool resourcesExist(pid_t owner)
{
    if (kTransport == "fifo")
        return access(fifoPath(owner, "h2c").c_str(), F_OK) == 0 && access(fifoPath(owner, "c2h").c_str(), F_OK) == 0;
    if (kTransport == "mq") {
        bool all = true;
        for (const char* direction : {"h2c", "c2h"}) {
            const mqd_t queue = mq_open(("/os_lab2_" + std::to_string(owner) + "_" + direction).c_str(), O_RDONLY | O_NONBLOCK);
            if (queue == static_cast<mqd_t>(-1))
                all = false;
            else
                mq_close(queue);
        }
        return all;
    }
    return false;
}

int pump(Connection& connection, Party self, int count)
{
    int sent = 0;
    int received = 0;
    const auto start = Clock::now();
    while ((sent < count || received < count) && elapsedMs(start) < 10000) {
        if (sent < count && say(connection, self, Party::Everyone, std::to_string(sent)) == SendResult::Ok)
            ++sent;
        WireMessage message;
        while (connection.receive(message) == RecvResult::Message) {
            if (textOf(message) != std::to_string(received))
                return 1;
            ++received;
        }
        pollfd descriptor{connection.readFd(), POLLIN, 0};
        poll(&descriptor, 1, 1);
    }
    return sent == count && received == count ? 0 : 2;
}

void exchange()
{
    const auto connection = createConnection();
    const pid_t pid = spawn([&] {
        if (!connection->connect(Party::Client))
            return 1;
        WireMessage message;
        if (receiveWithin(*connection, message, 2000) != RecvResult::Message || textOf(message) != "ping"
            || message.sender != Party::Host || message.recipient != Party::Everyone)
            return 2;
        return say(*connection, Party::Client, Party::Host, "pong") == SendResult::Ok ? 0 : 3;
    });

    CHECK(connection->connect(Party::Host));
    CHECK(say(*connection, Party::Host, Party::Everyone, "ping") == SendResult::Ok);
    WireMessage reply;
    CHECK(receiveWithin(*connection, reply, 2000) == RecvResult::Message);
    CHECK(textOf(reply) == "pong");
    CHECK(reply.sender == Party::Client && reply.recipient == Party::Host);
    CHECK(waitFor(pid) == 0);
}

int falseEofRounds(int rounds)
{
    setpriority(PRIO_PROCESS, 0, 19);
    for (int i = 0; i < rounds; ++i) {
        const auto connection = createConnection();
        const pid_t pid = spawn([&] {
            if (!connection->connect(Party::Client))
                return 1;
            WireMessage message;
            return connection->receive(message) == RecvResult::Empty ? 0 : 2;
        });
        if (!connection->connect(Party::Host)) {
            kill(pid, SIGKILL);
            waitFor(pid);
            return 3;
        }
        if (waitFor(pid) != 0)
            return 4;
    }
    return 0;
}

void noFalseEofAfterConnect()
{
    const auto connection = createConnection();
    const pid_t pid = spawn([&] {
        if (!connection->connect(Party::Client))
            return 1;
        WireMessage message;
        if (connection->receive(message) != RecvResult::Empty)
            return 2;
        return receivedText(*connection, "late") ? 0 : 3;
    });

    CHECK(connection->connect(Party::Host));
    WireMessage message;
    CHECK(connection->receive(message) == RecvResult::Empty);
    usleep(200000);
    CHECK(say(*connection, Party::Host, Party::Everyone, "late") == SendResult::Ok);
    CHECK(waitFor(pid) == 0);

    CHECK(waitFor(spawn([] { return falseEofRounds(50); })) == 0);
}

void bulkBothWays()
{
    const auto connection = createConnection();
    const pid_t pid = spawn([&] {
        return connection->connect(Party::Client) ? pump(*connection, Party::Client, 1000) : 1;
    });

    CHECK(connection->connect(Party::Host));
    CHECK(pump(*connection, Party::Host, 1000) == 0);
    CHECK(waitFor(pid) == 0);
}

void overflowAndRecovery()
{
    int go[2];
    CHECK(pipe(go) == 0);
    const auto connection = createConnection();
    const pid_t pid = spawn([&] {
        if (!connection->connect(Party::Client))
            return 1;
        pollfd descriptor{go[0], POLLIN, 0};
        char byte;
        if (poll(&descriptor, 1, 5000) != 1 || read(go[0], &byte, 1) != 1)
            return 2;
        for (unsigned i = 0; i < kSlots; ++i)
            if (!receivedText(*connection, std::to_string(i)))
                return 3;
        if (say(*connection, Party::Client, Party::Host, "drained") != SendResult::Ok)
            return 4;
        for (unsigned i = kSlots; i < 2 * kSlots; ++i)
            if (!receivedText(*connection, std::to_string(i)))
                return 5;
        return 0;
    });

    CHECK(connection->connect(Party::Host));
    const auto start = Clock::now();
    unsigned accepted = 0;
    SendResult last = SendResult::Ok;
    while (last == SendResult::Ok && accepted < 100) {
        last = say(*connection, Party::Host, Party::Everyone, std::to_string(accepted));
        if (last == SendResult::Ok)
            ++accepted;
    }
    CHECK(accepted == kSlots);
    CHECK(last == SendResult::Busy);
    CHECK(elapsedMs(start) < 500);

    CHECK(write(go[1], "x", 1) == 1);
    CHECK(receivedText(*connection, "drained"));
    for (unsigned i = kSlots; i < 2 * kSlots; ++i)
        CHECK(say(*connection, Party::Host, Party::Everyone, std::to_string(i)) == SendResult::Ok);
    CHECK(waitFor(pid) == 0);
    close(go[0]);
    close(go[1]);
}

void partialFrameIsAssembled()
{
    if (kTransport != "fifo")
        return;

    int ack[2];
    CHECK(pipe(ack) == 0);
    const auto connection = createConnection();
    const pid_t pid = spawn([&] {
        if (!connection->connect(Party::Client))
            return 1;
        WireMessage message;
        if (receiveWithin(*connection, message, 2000) != RecvResult::Empty)
            return 2;
        if (write(ack[1], "x", 1) != 1)
            return 3;
        return receiveWithin(*connection, message, 2000) == RecvResult::Message && textOf(message) == "whole" ? 0 : 4;
    });

    CHECK(connection->connect(Party::Host));
    const int writer = open(fifoPath(getpid(), "h2c").c_str(), O_WRONLY | O_NONBLOCK);
    CHECK(writer >= 0);

    const WireMessage frame = *makeText(Party::Host, Party::Everyone, "whole");
    const char* bytes = reinterpret_cast<const char*>(&frame);
    const std::size_t half = sizeof frame / 2;
    CHECK(write(writer, bytes, half) == static_cast<ssize_t>(half));

    pollfd descriptor{ack[0], POLLIN, 0};
    CHECK(poll(&descriptor, 1, 2000) == 1);
    CHECK(write(writer, bytes + half, sizeof frame - half) == static_cast<ssize_t>(sizeof frame - half));
    CHECK(waitFor(pid) == 0);
    close(writer);
    close(ack[0]);
    close(ack[1]);
}

void detectsClosedPeer()
{
    const bool reportsEof = kTransport != "mq";
    const auto connection = createConnection();
    const pid_t pid = spawn([&] { return connection->connect(Party::Client) ? 0 : 1; });

    CHECK(connection->connect(Party::Host));
    CHECK(waitFor(pid) == 0);
    WireMessage message;
    if (reportsEof) {
        CHECK(receiveWithin(*connection, message, 1000) == RecvResult::Closed);
        CHECK(say(*connection, Party::Host, Party::Everyone, "anyone?") == SendResult::Closed);
    } else {
        CHECK(receiveWithin(*connection, message, 200) == RecvResult::Empty);
        CHECK(say(*connection, Party::Host, Party::Everyone, "anyone?") == SendResult::Ok);
    }
}

void releasesResources()
{
    const bool named = kTransport == "fifo" || kTransport == "mq";
    const pid_t owner = getpid();
    {
        auto connection = createConnection();
        const pid_t pid = spawn([&] {
            const bool connected = connection->connect(Party::Client);
            connection.reset();
            return connected ? 0 : 1;
        });

        CHECK(connection->connect(Party::Host));
        CHECK(resourcesExist(owner) == named);
        CHECK(waitFor(pid) == 0);
        CHECK(resourcesExist(owner) == named);
    }
    CHECK(!resourcesExist(owner));
}

void hostTimesOutWithoutClient()
{
    const auto connection = createConnection();
    const pid_t pid = spawn([] {
        sleep(2);
        return 0;
    });

    const auto start = Clock::now();
    CHECK(!connection->connect(Party::Host, 300));
    const long ms = elapsedMs(start);
    CHECK(ms >= 250 && ms < 1500);
    CHECK(!connection->error().empty());
    CHECK(say(*connection, Party::Host, Party::Everyone, "x") == SendResult::Error);

    kill(pid, SIGKILL);
    CHECK(waitFor(pid) == 128 + SIGKILL);
}

void clientTimesOutWithoutHost()
{
    const auto connection = createConnection();
    const pid_t pid = spawn([&] {
        const auto start = Clock::now();
        const bool connected = connection->connect(Party::Client, 300);
        const long ms = elapsedMs(start);
        return (!connected && ms >= 250 && ms < 1500 && !connection->error().empty()) ? 0 : 1;
    });
    CHECK(waitFor(pid) == 0);
}

void noZombies()
{
    errno = 0;
    CHECK(waitpid(-1, nullptr, WNOHANG) == -1 && errno == ECHILD);
}

}

int main()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::signal(SIGPIPE, SIG_IGN);

    RUN(exchange);
    RUN(noFalseEofAfterConnect);
    RUN(bulkBothWays);
    RUN(overflowAndRecovery);
    RUN(partialFrameIsAssembled);
    RUN(detectsClosedPeer);
    RUN(releasesResources);
    RUN(hostTimesOutWithoutClient);
    RUN(clientTimesOutWithoutHost);
    RUN(noZombies);
    return g_failures == 0 ? 0 : 1;
}
