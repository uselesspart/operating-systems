// Тесты класса Conn. Один и тот же файл линкуется с conn_pipe / conn_fifo / conn_sock,
// поэтому все типы взаимодействия проверяются одинаковыми сценариями.
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <atomic>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "conn.h"
#include "protocol.h"
#include "sem.h"
#include "testlib.h"

// Запускает «клиента» как дочерний процесс; body выполняется в ребёнке после Open().
static pid_t fork_client(Conn& host, int id, std::function<int(Conn&)> body) {
    const std::string spec = host.Spec();
    pid_t pid = fork();
    if (pid == 0) {
        try {
            Conn c(id, false, spec);
            if (!c.Open()) _exit(90);
            _exit(body(c));
        } catch (...) {
            _exit(91);
        }
    }
    host.CloseChildEnds();
    return pid;
}

static bool wait_ok(pid_t pid) {
    int st = 0;
    waitpid(pid, &st, 0);
    return WIFEXITED(st) && WEXITSTATUS(st) == 0;
}

// 1. Обмен 1000 сообщениями «запрос-ответ»: целостность и порядок.
static void test_roundtrip() {
    Conn h(1, true);
    pid_t pid = fork_client(h, 1, [](Conn& c) {
        for (int i = 0; i < 1000; ++i) {
            Msg m{};
            if (c.Read(&m, sizeof m, 5000) <= 0) return 1;
            m.from += 1000;
            if (!c.Write(&m, sizeof m)) return 2;
        }
        return 0;
    });
    CHECK(h.Open());
    for (int i = 0; i < 1000 && g_fail == 0; ++i) {
        Msg m{};
        m.type = MT_CHAT;
        m.from = i;
        set_text(m, "msg " + std::to_string(i));
        CHECK(h.Write(&m, sizeof m));
        Msg r{};
        CHECK(h.Read(&r, sizeof r, 5000) == 1);
        CHECK(r.from == i + 1000);
        CHECK(std::strcmp(r.text, m.text) == 0);
    }
    CHECK(wait_ok(pid));
}

// 2. Тайм-аут чтения: если данных нет — 0 примерно через указанное время.
static void test_read_timeout() {
    Conn h(1, true);
    pid_t pid = fork_client(h, 1, [](Conn&) {
        usleep(1500 * 1000);
        return 0;
    });
    CHECK(h.Open());
    Msg m{};
    long long t0 = test_now_ms();
    CHECK(h.Read(&m, sizeof m, 300) == 0);
    long long el = test_now_ms() - t0;
    CHECK(el >= 250 && el < 1000);
    CHECK(wait_ok(pid));
}

// 3. Тайм-аут записи: собеседник не читает — Write в итоге возвращает false.
static void test_write_timeout() {
    Conn h(1, true);
    pid_t pid = fork_client(h, 1, [](Conn&) {
        usleep(4000 * 1000);
        return 0;
    });
    CHECK(h.Open());
    Msg m{};
    m.type = MT_CHAT;
    int n = 0;
    bool failed = false;
    while (n < 20000) {
        if (!h.Write(&m, sizeof m, 200)) {
            failed = true;
            break;
        }
        ++n;
    }
    CHECK(failed);
    CHECK(n > 0);
    kill(pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

// 4. Корректное завершение собеседника: чтение видит обрыв (<0), запись — ошибку.
static void test_peer_exit() {
    Conn h(1, true);
    pid_t pid = fork_client(h, 1, [](Conn&) {
        usleep(300 * 1000);
        return 0;
    });
    CHECK(h.Open());
    waitpid(pid, nullptr, 0);
    Msg m{};
    long long t0 = test_now_ms();
    CHECK(h.Read(&m, sizeof m, 2000) < 0);
    CHECK(test_now_ms() - t0 < 2000);
    bool failed = false;
    for (int i = 0; i < 3 && !failed; ++i) failed = !h.Write(&m, sizeof m, 500);
    CHECK(failed);
}

// 5. Принудительное завершение (SIGKILL, как за неактивность): хост замечает обрыв.
static void test_peer_sigkill() {
    Conn h(1, true);
    pid_t pid = fork_client(h, 1, [](Conn&) {
        usleep(10 * 1000 * 1000);
        return 0;
    });
    CHECK(h.Open());
    kill(pid, SIGKILL);
    int st = 0;
    waitpid(pid, &st, 0);
    CHECK(WIFSIGNALED(st) && WTERMSIG(st) == SIGKILL);
    Msg m{};
    long long t0 = test_now_ms();
    CHECK(h.Read(&m, sizeof m, 2000) < 0);
    CHECK(test_now_ms() - t0 < 2000);
}

// 6. Один ко многим: 8 клиентов, по потоку на клиента, у каждого 200 сообщений по порядку.
static void test_many_clients() {
    const int N = 8, M = 200;
    struct P {
        std::unique_ptr<Conn> c;
        pid_t pid = -1;
    };
    std::vector<P> ps(N);
    for (int i = 0; i < N; ++i) {
        const int id = i + 1;
        ps[i].c.reset(new Conn(id, true));
        ps[i].pid = fork_client(*ps[i].c, id, [id](Conn& c) {
            for (int k = 0; k < M; ++k) {
                Msg m{};
                m.type = MT_CHAT;
                m.from = id;
                m.ts = k;
                set_text(m, "x");
                if (!c.Write(&m, sizeof m)) return 1;
            }
            Msg r{};
            if (c.Read(&r, sizeof r, 5000) <= 0) return 2;
            return r.type == MT_USERS ? 0 : 3;
        });
    }
    Sem lock;
    int total = 0;
    std::atomic<int> errors{0};
    std::vector<std::thread> th;  // потоки создаются после всех fork
    for (int i = 0; i < N; ++i) {
        th.emplace_back([&, i] {
            Conn& c = *ps[i].c;
            if (!c.Open()) {
                ++errors;
                return;
            }
            for (int k = 0; k < M; ++k) {
                Msg m{};
                if (c.Read(&m, sizeof m, 5000) != 1 || m.from != i + 1 || m.ts != k) {
                    ++errors;
                    return;
                }
                SemGuard g(lock);
                ++total;
            }
            Msg r{};
            r.type = MT_USERS;
            if (!c.Write(&r, sizeof r)) ++errors;
        });
    }
    for (auto& t : th) t.join();
    CHECK(errors == 0);
    CHECK(total == N * M);
    for (auto& p : ps) CHECK(wait_ok(p.pid));
}

// 7. Для fifo: файлы именованных каналов удаляются хостом после работы.
static void test_fifo_cleanup() {
    if (std::strcmp(Conn::TypeName(), "fifo") != 0) return;
    std::string base;
    {
        Conn h(1, true);
        pid_t pid = fork_client(h, 1, [](Conn&) {
            usleep(300 * 1000);
            return 0;
        });
        CHECK(h.Open());
        base = h.Spec();
        CHECK(access((base + "_c2h").c_str(), F_OK) == 0);
        CHECK(access((base + "_h2c").c_str(), F_OK) == 0);
        CHECK(wait_ok(pid));
    }
    CHECK(access((base + "_c2h").c_str(), F_OK) != 0);
    CHECK(access((base + "_h2c").c_str(), F_OK) != 0);
}

// 8. Неверное описание канала: конструктор клиента выбрасывает исключение.
static void test_bad_spec() {
    bool threw = false;
    try {
        Conn c(1, false, "");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

int main() {
    signal(SIGPIPE, SIG_IGN);
    std::printf("test_conn [%s]\n", Conn::TypeName());
    test_roundtrip();
    test_read_timeout();
    test_write_timeout();
    test_peer_exit();
    test_peer_sigkill();
    test_many_clients();
    test_fifo_cleanup();
    test_bad_spec();
    std::printf("test_conn [%s]: %d проверок, %d ошибок\n", Conn::TypeName(), g_checks, g_fail);
    return g_fail ? 1 : 0;
}
