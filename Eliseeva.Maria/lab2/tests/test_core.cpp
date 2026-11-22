// Модульные тесты общих частей: протокол, семафор с тайм-аутом, ввод-вывод с тайм-аутами.
#include <signal.h>
#include <unistd.h>

#include <cstring>
#include <string>
#include <thread>
#include <vector>

#include "conn_util.h"
#include "protocol.h"
#include "sem.h"
#include "testlib.h"

static void test_protocol() {
    CHECK(sizeof(Msg) == 256);

    Msg m{};
    set_text(m, "привет");
    CHECK(std::string(m.text) == "привет");

    // длинный текст из двухбайтных символов обрезается по границе символа
    std::string longs;
    for (int i = 0; i < 300; ++i) longs += "я";
    set_text(m, longs);
    size_t n = std::strlen(m.text);
    CHECK(n <= sizeof(m.text) - 1);
    CHECK(n % 2 == 0);  // не разрезали символ посередине
    CHECK(m.text[sizeof(m.text) - 1] == 0);

    Msg b{};
    b.from = 2;
    b.to = ID_ALL;
    b.ts = wall_ms();
    set_text(b, "hi");
    std::string lb = format_line(b);
    CHECK(lb.find("Клиент 2: hi") != std::string::npos);
    CHECK(lb.find("(лично)") == std::string::npos);

    Msg p = b;
    p.to = 3;
    std::string lp = format_line(p);
    CHECK(lp.find("(лично)") != std::string::npos);
    CHECK(lp.find("Клиент 3") != std::string::npos);

    CHECK(who(ID_HOST) == "Хост");
    CHECK(who(ID_ALL) == "Всем");
    CHECK(who(7) == "Клиент 7");
}

static void test_sem() {
    Sem s(1);
    CHECK(s.Lock(100));  // свободный семафор берётся сразу

    bool ok = true;
    long long el = 0;
    std::thread t([&] {
        long long t0 = test_now_ms();
        ok = s.Lock(300);  // занят — должен вернуть false по тайм-ауту
        el = test_now_ms() - t0;
    });
    t.join();
    CHECK(!ok);
    CHECK(el >= 250 && el < 1500);

    s.Unlock();
    {
        SemGuard g(s, 100);
        CHECK(g.ok());
        SemGuard g2(s, 100);  // второй захват того же семафора — тайм-аут
        CHECK(!g2.ok());
    }
    CHECK(s.Lock(100));  // guard всё освободил
}

static void test_io_timeouts() {
    using namespace connutil;
    int p[2];
    CHECK(pipe(p) == 0);
    set_nonblock(p[0]);
    set_nonblock(p[1]);
    char buf[256];

    // нет данных: тайм-аут, возвращается 0
    long long t0 = test_now_ms();
    CHECK(read_all(p[0], buf, sizeof buf, 200) == 0);
    long long el = test_now_ms() - t0;
    CHECK(el >= 150 && el < 1000);

    // пришла только часть сообщения: ошибка (-1)
    CHECK(write(p[1], "0123456789", 10) == 10);
    CHECK(read_all(p[0], buf, sizeof buf, 200) == -1);

    // целое сообщение читается
    char msg[256];
    std::memset(msg, 'x', sizeof msg);
    CHECK(write_all(p[1], msg, sizeof msg, 200));
    CHECK(read_all(p[0], buf, sizeof buf, 200) == 1);
    CHECK(std::memcmp(buf, msg, sizeof msg) == 0);

    // запись в переполненный канал заканчивается тайм-аутом
    std::vector<char> big(1 << 20, 'z');
    t0 = test_now_ms();
    CHECK(!write_all(p[1], big.data(), big.size(), 200));
    el = test_now_ms() - t0;
    CHECK(el < 1500);

    close(p[0]);
    close(p[1]);

    // обрыв: после закрытия записи чтение даёт -1
    CHECK(pipe(p) == 0);
    set_nonblock(p[0]);
    close(p[1]);
    CHECK(read_all(p[0], buf, sizeof buf, 500) == -1);
    close(p[0]);

    // запись в канал без читателя: false (без SIGPIPE)
    CHECK(pipe(p) == 0);
    set_nonblock(p[1]);
    close(p[0]);
    CHECK(!write_all(p[1], "a", 1, 200));
    close(p[1]);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    test_protocol();
    test_sem();
    test_io_timeouts();
    std::printf("test_core: %d проверок, %d ошибок\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
