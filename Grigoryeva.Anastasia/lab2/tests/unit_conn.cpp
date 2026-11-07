// Модульные тесты соединения. Собирается отдельно для каждого типа (conn_pipe, conn_fifo, conn_sock, ...) -
// все типы обязаны вести себя одинаково, т.к. имеют общий интерфейс Conn.
// Хост и клиент создаются в одном процессе (для pipe клиент открывает /proc/<свой pid>/fd/N).
#include <semaphore.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <thread>

#include "conn.h"
#include "fd_io.h"
#include "protocol.h"
#include "sync_sem.h"

static int g_total = 0, g_failed = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_total;                                                                  \
        if (!(cond)) {                                                              \
            ++g_failed;                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; \
        }                                                                           \
    } while (0)

static bool SemExists(const ConnId& id, const char* suffix) {
    std::string name = "/" + ConnName(id) + "_" + suffix;
    sem_t* s = sem_open(name.c_str(), 0);
    if (s == SEM_FAILED) return false;
    sem_close(s);
    return true;
}

static void testRoundTrip() {
    ConnId id{11, getpid()};
    Conn host(id, true);
    Conn client(id, false);

    Message out = MakeMessage(MSG_REQUEST, 42), in{};
    CHECK(host.Write(&out, sizeof(out)));
    CHECK(client.Read(&in, sizeof(in)));
    CHECK(in.type == MSG_REQUEST && in.value == 42);

    out = MakeMessage(MSG_NUMBER, 77);
    CHECK(client.Write(&out, sizeof(out)));
    CHECK(host.Read(&in, sizeof(in)));
    CHECK(in.type == MSG_NUMBER && in.value == 77);
}

static void testOrderPreserved() {
    ConnId id{12, getpid()};
    Conn host(id, true);
    Conn client(id, false);
    for (int i = 0; i < 50; ++i) {
        Message m = MakeMessage(MSG_PING, i);
        CHECK(host.Write(&m, sizeof(m)));
    }
    bool ordered = true;
    for (int i = 0; i < 50; ++i) {
        Message m{};
        if (!client.Read(&m, sizeof(m)) || m.value != i) ordered = false;
    }
    CHECK(ordered);
}

static void testBlockingReadWakesUp() {
    ConnId id{13, getpid()};
    Conn host(id, true);
    Conn client(id, false);
    Message got{};
    bool ok = false;
    std::thread reader([&] { ok = client.Read(&got, sizeof(got)); });  // блокируется, пока нет данных
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    Message m = MakeMessage(MSG_STATUS, kStatusAlive);
    CHECK(host.Write(&m, sizeof(m)));
    reader.join();
    CHECK(ok);
    CHECK(got.type == MSG_STATUS && got.value == kStatusAlive);
}

static void testConcurrentStream() {
    ConnId id{14, getpid()};
    Conn host(id, true);
    Conn client(id, false);
    const int N = 2000;
    std::thread writer([&] {
        for (int i = 0; i < N; ++i) {
            Message m = MakeMessage(MSG_NUMBER, i);
            host.Write(&m, sizeof(m));
        }
    });
    bool ordered = true;
    for (int i = 0; i < N; ++i) {
        Message m{};
        if (!client.Read(&m, sizeof(m)) || m.value != i) {
            ordered = false;
            break;
        }
    }
    writer.join();
    CHECK(ordered);
}

static void testReadTimeout() {
    ConnId id{15, getpid()};
    Conn host(id, true);
    Conn client(id, false);
    SetIoTimeoutMs(300);
    Message m{};
    auto t0 = std::chrono::steady_clock::now();
    bool ok = client.Read(&m, sizeof(m));  // данных нет -> тайм-аут
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    CHECK(!ok);
    CHECK(ms >= 250 && ms < 2000);
    CHECK(!client.LastError().empty());
    SetIoTimeoutMs(5000);
}

static void testClientWithoutHostFails() {
    bool thrown = false;
    try {
        Conn c(ConnId{99, getpid()}, false);
    } catch (const std::exception&) {
        thrown = true;
    }
    CHECK(thrown);
}

static void testResourcesReleased() {
    ConnId id{16, getpid()};
    {
        Conn host(id, true);
        CHECK(SemExists(id, "h2c"));
        CHECK(SemExists(id, "c2h"));
    }
    CHECK(!SemExists(id, "h2c"));  // хост удалил за собой глобальные семафоры
    CHECK(!SemExists(id, "c2h"));
}

static void testDefaultTimeoutIsFiveSeconds() {
    SetIoTimeoutMs(0);
    CHECK(IoTimeoutMs() == 5000);
}

int main() {
    testRoundTrip();
    testOrderPreserved();
    testBlockingReadWakesUp();
    testConcurrentStream();
    testReadTimeout();
    testClientWithoutHostFails();
    testResourcesReleased();
    testDefaultTimeoutIsFiveSeconds();
    std::cout << "unit_conn: проверок " << g_total << ", провалено " << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
