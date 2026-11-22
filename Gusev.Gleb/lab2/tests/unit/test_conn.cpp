#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include "conn/conn.h"
#include "ipc/Names.h"
#include "ipc/Protocol.h"
#include "test.h"

TEST_SUITE("Conn — канал волк ↔ козлёнок после fork");

using namespace std::chrono;
using namespace std::chrono_literals;
using wolf::Conn;
using wolf::ConnId;
namespace fs = std::filesystem;

namespace {

ConnId nextId() {
    static int n = 0;
    return ConnId{getpid(), ++n};
}

int exitCode(pid_t pid) {
    int status = 0;
    ::waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

template <typename Body>
pid_t spawnKid(const ConnId& id, Body body) {
    Conn::prepare(id);
    const pid_t pid = ::fork();
    if (pid == 0) {
        int rc = 1;
        try {
            Conn conn(id, false);
            rc = body(conn) ? 0 : 1;
        } catch (...) {
            rc = 3;
        }
        ::_exit(rc);
    }
    return pid;
}

int filesInRuntimeDir() {
    const std::string dir = wolf::names::runtimeDir(getpid());
    if (!fs::exists(dir)) return 0;
    int n = 0;
    for ([[maybe_unused]] const auto& entry : fs::directory_iterator(dir)) ++n;
    return n;
}

struct Setup {
    Setup() { ::signal(SIGPIPE, SIG_IGN); }
    ~Setup() { ::rmdir(wolf::names::runtimeDir(getpid()).c_str()); }
} setup;

}

TEST(type_code, "typeCode совпадает с типом сборки") {
    const std::string type = Conn::typeCode();
    EXPECT_TRUE(type == "pipe" || type == "fifo" || type == "sock");
    CONTEXT("тип: " << type);
}

TEST(message_roundtrip, "сообщение туда и ответ обратно доходят без искажений") {
    const ConnId id = nextId();
    const pid_t kid = spawnKid(id, [](Conn& c) {
        wolf::proto::Message m;
        if (!c.Read(&m, sizeof m) || !wolf::proto::isValid(m) || m.round != 5) return false;
        const auto reply = wolf::proto::makeThrow(m.round, 77);
        return c.Write(&reply, sizeof reply);
    });
    Conn host(id, true);
    const auto round = wolf::proto::makeRound(5, true, 3);
    ASSERT_TRUE(host.Write(&round, sizeof round));
    wolf::proto::Message reply;
    ASSERT_TRUE(host.Read(&reply, sizeof reply));
    EXPECT_TRUE(wolf::proto::isValid(reply));
    EXPECT_EQ(reply.number, 77);
    EXPECT_EQ(reply.round, 5u);
    EXPECT_EQ(exitCode(kid), 0);
}

TEST(large_payload, "256 КБ в обе стороны — данные целы") {
    const ConnId id = nextId();
    constexpr std::size_t kSize = 256 * 1024;
    const pid_t kid = spawnKid(id, [](Conn& c) {
        std::vector<char> buf(kSize);
        return c.Read(buf.data(), buf.size()) && c.Write(buf.data(), buf.size());
    });
    Conn host(id, true);
    std::vector<char> out(kSize);
    for (std::size_t i = 0; i < kSize; ++i) out[i] = static_cast<char>(i * 31 + 7);
    ASSERT_TRUE(host.Write(out.data(), out.size()));
    std::vector<char> in(kSize);
    ASSERT_TRUE(host.Read(in.data(), in.size()));
    EXPECT_TRUE(std::memcmp(in.data(), out.data(), kSize) == 0);
    EXPECT_EQ(exitCode(kid), 0);
}

TEST(peer_exit_detected, "козлёнок завершился — Read хоста сразу возвращает false") {
    const ConnId id = nextId();
    const pid_t kid = spawnKid(id, [](Conn& c) {
        wolf::proto::Message hello;
        const auto m = wolf::proto::makeThrow(1, 1);
        return c.Read(&hello, sizeof hello) && c.Write(&m, sizeof m);
    });
    Conn host(id, true);
    const auto hello = wolf::proto::makeRound(1, true, 1);
    host.Write(&hello, sizeof hello);
    wolf::proto::Message m;
    ASSERT_TRUE(host.Read(&m, sizeof m));
    EXPECT_EQ(exitCode(kid), 0);
    const auto start = steady_clock::now();
    EXPECT_FALSE(host.Read(&m, sizeof m));
    const auto ms = duration_cast<milliseconds>(steady_clock::now() - start).count();
    CONTEXT("Read вернулся через " << ms << " мс");
    EXPECT_TRUE(ms < 1000);
}

TEST(write_to_dead_peer, "запись мёртвому козлёнку — false, процесс хоста не падает") {
    const ConnId id = nextId();
    const pid_t kid = spawnKid(id, [](Conn& c) {
        wolf::proto::Message hello;
        const auto m = wolf::proto::makeThrow(1, 1);
        return c.Read(&hello, sizeof hello) && c.Write(&m, sizeof m);
    });
    Conn host(id, true);
    const auto hello = wolf::proto::makeRound(1, true, 1);
    host.Write(&hello, sizeof hello);
    wolf::proto::Message m;
    host.Read(&m, sizeof m);
    exitCode(kid);
    std::vector<char> big(256 * 1024, 'x');
    EXPECT_FALSE(host.Write(big.data(), big.size()));
}

TEST(silent_peer_times_out, "козлёнок подключился, но молчит — Read ждёт ~5 с и возвращает false") {
    const ConnId id = nextId();
    Conn::prepare(id);
    const pid_t kid = ::fork();
    if (kid == 0) {
        try {
            Conn conn(id, false);
            ::usleep(6 * 1000 * 1000);
        } catch (...) {
            ::_exit(3);
        }
        ::_exit(0);
    }
    Conn host(id, true);
    const auto start = steady_clock::now();
    wolf::proto::Message m;
    EXPECT_FALSE(host.Read(&m, sizeof m));
    const auto ms = duration_cast<milliseconds>(steady_clock::now() - start).count();
    CONTEXT("ждали " << ms << " мс");
    EXPECT_TRUE(ms >= 4800 && ms < 7000);
    ::kill(kid, SIGKILL);
    exitCode(kid);
}

TEST(attach_to_missing_throws, "подключение к неподготовленному каналу — std::system_error") {
    bool thrown = false;
    try {
        Conn kid(ConnId{getpid(), 9999}, false);
    } catch (const std::system_error& e) {
        thrown = true;
        CONTEXT("ошибка: " << e.what());
    }
    EXPECT_TRUE(thrown);
}

TEST(host_cleans_up, "хост удаляет файлы FIFO при уничтожении Conn") {
    const int before = filesInRuntimeDir();
    {
        const ConnId id = nextId();
        Conn::prepare(id);
        Conn host(id, true);
        if (std::string(Conn::typeCode()) == "fifo") {
            EXPECT_TRUE(filesInRuntimeDir() > before);
        }
    }
    EXPECT_EQ(filesInRuntimeDir(), before);
    ::rmdir(wolf::names::runtimeDir(getpid()).c_str());
}

TEST(two_kids_independent, "два козлёнка с разными номерами не мешают друг другу") {
    const ConnId a = nextId();
    const ConnId b = nextId();
    auto echoNumber = [](Conn& c) {
        wolf::proto::Message m;
        if (!c.Read(&m, sizeof m)) return false;
        const auto reply = wolf::proto::makeThrow(m.round, static_cast<int>(m.kids));
        return c.Write(&reply, sizeof reply);
    };
    const pid_t kidA = spawnKid(a, echoNumber);
    const pid_t kidB = spawnKid(b, echoNumber);
    Conn hostA(a, true);
    Conn hostB(b, true);
    const auto ma = wolf::proto::makeRound(1, true, 11);
    const auto mb = wolf::proto::makeRound(1, true, 22);
    ASSERT_TRUE(hostB.Write(&mb, sizeof mb));
    ASSERT_TRUE(hostA.Write(&ma, sizeof ma));
    wolf::proto::Message ra;
    wolf::proto::Message rb;
    ASSERT_TRUE(hostA.Read(&ra, sizeof ra));
    ASSERT_TRUE(hostB.Read(&rb, sizeof rb));
    EXPECT_EQ(ra.number, 11);
    EXPECT_EQ(rb.number, 22);
    EXPECT_EQ(exitCode(kidA), 0);
    EXPECT_EQ(exitCode(kidB), 0);
}
