#include <chrono>
#include <csignal>
#include <functional>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

#include "conn.h"
#include "kid.h"
#include "protocol.h"

using proto::Msg;

namespace {

const char* const VERDICT[] = {"Провален", "Пройден"};

struct PairResult {
    bool hostOk;
    int childRc;
};

PairResult runPair(const std::function<bool(Conn&)>& host, const std::function<int(Conn&)>& client) {
    const pid_t hostPid = getpid();
    Conn* conn = nullptr;
    if (Conn::kCreateBeforeFork) conn = new Conn(hostPid, true);

    std::cout.flush();
    pid_t kid = fork();
    if (kid == 0) {
        if (!Conn::kCreateBeforeFork) conn = new Conn(hostPid, false);
        int rc = (conn && conn->Ok()) ? client(*conn) : 99;
        delete conn;
        _exit(rc);
    }

    if (!Conn::kCreateBeforeFork) conn = new Conn(hostPid, true);
    bool hostOk = conn && conn->Ok() && host(*conn);

    int status = 0;
    bool exited = false;
    for (int i = 0; i < 300 && !exited; ++i) {
        if (waitpid(kid, &status, WNOHANG) == kid) exited = true;
        else usleep(50 * 1000);
    }
    if (!exited) {
        kill(kid, SIGKILL);
        waitpid(kid, &status, 0);
    }
    delete conn;
    return {hostOk, exited && WIFEXITED(status) ? WEXITSTATUS(status) : -1};
}

bool sendMsg(Conn& c, int type, int value) {
    Msg m{type, value};
    return c.Write(&m, sizeof(m));
}

bool readNumber(Conn& c, int lo, int hi) {
    Msg m{};
    return c.Read(&m, sizeof(m)) && m.type == proto::kNumber && m.value >= lo && m.value <= hi;
}

bool testMessageGoesThereAndBack() {
    auto r = runPair(
        [](Conn& c) {
            Msg in{};
            return sendMsg(c, proto::kNumber, 42) && c.Read(&in, sizeof(in)) && in.value == 43;
        },
        [](Conn& c) {
            Msg in{};
            if (!c.Read(&in, sizeof(in))) return 1;
            return sendMsg(c, proto::kStatus, in.value + 1) ? 0 : 1;
        });

    return r.hostOk && r.childRc == 0;
}

bool testManyMessagesKeepOrder() {
    const int count = 300;
    auto r = runPair(
        [count](Conn& c) {
            for (int i = 0; i < count; ++i) {
                Msg in{};
                if (!sendMsg(c, proto::kNumber, i) || !c.Read(&in, sizeof(in)) || in.value != i * 2)
                    return false;
            }
            return true;
        },
        [count](Conn& c) {
            for (int i = 0; i < count; ++i) {
                Msg in{};
                if (!c.Read(&in, sizeof(in)) || in.value != i) return 1;
                if (!sendMsg(c, proto::kStatus, i * 2)) return 1;
            }
            return 0;
        });

    return r.hostOk && r.childRc == 0;
}

bool testOversizedMessageIsRejected() {
    auto r = runPair(
        [](Conn& c) {
            char big[4096] = {0};
            return !c.Write(big, sizeof(big));
        },
        [](Conn&) { return 0; });

    return r.hostOk && r.childRc == 0;
}

bool testReadTimesOutAfterFiveSeconds() {
    using clock = std::chrono::steady_clock;
    auto r = runPair(
        [](Conn& c) {
            Msg in{};
            auto t0 = clock::now();
            bool ok = c.Read(&in, sizeof(in));
            auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();
            return !ok && ms >= 4500 && ms <= 7000;
        },
        [](Conn&) { return 0; });

    return r.hostOk && r.childRc == 0;
}

bool testKidPlaysUntilGameOver() {
    auto r = runPair(
        [](Conn& c) {
            Msg join{};
            return c.Read(&join, sizeof(join)) && join.type == proto::kJoin &&
                   sendMsg(c, proto::kStatus, 1) &&
                   readNumber(c, 1, proto::kKidMaxAlive) && sendMsg(c, proto::kStatus, 0) &&
                   readNumber(c, 1, proto::kKidMaxDead) && sendMsg(c, proto::kStatus, 1) &&
                   readNumber(c, 1, proto::kKidMaxAlive) && sendMsg(c, proto::kGameOver, 0);
        },
        [](Conn& c) { return RunKid(c); });

    return r.hostOk && r.childRc == 0;
}

bool testKidStopsWhenWolfIsSilent() {
    auto r = runPair(
        [](Conn& c) {
            Msg join{};
            bool ok = c.Read(&join, sizeof(join)) && join.type == proto::kJoin;
            sleep(7);
            return ok;
        },
        [](Conn& c) { return RunKid(c); });

    return r.hostOk && r.childRc == 1;
}

bool run(const char* name, bool (*test)()) {
    std::cout << "[ RUN    ] " << name << '\n';
    bool passed = false;
    try {
        passed = test();
    } catch (const std::exception& e) {
        std::cout << "  exception: " << e.what() << '\n';
    }
    std::cout << "[ " << VERDICT[passed] << " ] " << name << '\n';
    return passed;
}

} 

int main() {
    const std::pair<const char*, bool (*)()> tests[] = {
        {"testMessageGoesThereAndBack",       testMessageGoesThereAndBack},
        {"testManyMessagesKeepOrder",         testManyMessagesKeepOrder},
        {"testOversizedMessageIsRejected",    testOversizedMessageIsRejected},
        {"testReadTimesOutAfterFiveSeconds",  testReadTimesOutAfterFiveSeconds},
        {"testKidPlaysUntilGameOver",         testKidPlaysUntilGameOver},
        {"testKidStopsWhenWolfIsSilent",      testKidStopsWhenWolfIsSilent},
    };

    int failed = 0;
    for (const auto& [name, fn] : tests)
        failed += !run(name, fn);

    std::cout << "Провалено: " << failed << '\n';
    return failed;
}
