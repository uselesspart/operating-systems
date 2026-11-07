// Клиент игры "Волк и семеро козлят" (козленок). Независимый процесс: при запуске получает pid хоста.
//
// Использование: client_<тип> <pid_хоста> [--numbers ЖИВОЙ,МЕРТВЫЙ] [--seed N]
//   --numbers  фиксированные числа (для тестов): бросаемое живым (1..100) и мёртвым (1..50)
//   --seed     инициализация генератора случайных чисел
//
// Коды возврата: 0 - игра окончена/клиент остановлен пользователем, 1 - не удалось подключиться,
//                2 - связь с хостом потеряна (в т.ч. тайм-аут 5 с).
#include <signal.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <memory>
#include <random>
#include <string>

#include "conn.h"
#include "fd_io.h"
#include "game_rules.h"
#include "handshake.h"
#include "log_util.h"
#include "protocol.h"

namespace {

volatile sig_atomic_t g_stop = 0;
void OnStopSignal(int) { g_stop = 1; }

void Say(const std::string& msg) { LogLine("козленок " + std::to_string(getpid()), msg); }

void PrintUsage(const char* prog) {
    std::cerr << "Использование: " << prog << " <pid_хоста> [--numbers ЖИВОЙ,МЕРТВЫЙ] [--seed N]\n";
}

bool ParseLong(const std::string& s, long& out) {
    char* end = nullptr;
    long v = std::strtol(s.c_str(), &end, 10);
    if (s.empty() || *end != '\0') return false;
    out = v;
    return true;
}

}  // namespace

int main(int argc, char* argv[]) {
    BlockHandshakeSignal();  // SIGUSR1 (ответ хоста) принимаем только через sigtimedwait
    signal(SIGPIPE, SIG_IGN);
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = OnStopSignal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);

    if (argc < 2) {
        PrintUsage(argv[0]);
        return 1;
    }
    long hostPidL = 0;
    if (!ParseLong(argv[1], hostPidL) || hostPidL <= 1) {
        std::cerr << "Неверный pid хоста: " << argv[1] << "\n";
        PrintUsage(argv[0]);
        return 1;
    }
    const pid_t hostPid = static_cast<pid_t>(hostPidL);

    bool fixed = false;
    int fixedAlive = 0, fixedDead = 0;
    unsigned seed = std::random_device{}();
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--numbers" && i + 1 < argc) {
            std::string v = argv[++i];
            size_t comma = v.find(',');
            long x = 0, y = 0;
            if (comma == std::string::npos || !ParseLong(v.substr(0, comma), x) || !ParseLong(v.substr(comma + 1), y) ||
                !rules::IsValidKidNumber(true, static_cast<int>(x)) || !rules::IsValidKidNumber(false, static_cast<int>(y))) {
                std::cerr << "--numbers: нужен формат ЖИВОЙ(1..100),МЕРТВЫЙ(1..50)\n";
                return 1;
            }
            fixed = true;
            fixedAlive = static_cast<int>(x);
            fixedDead = static_cast<int>(y);
        } else if (a == "--seed" && i + 1 < argc) {
            long s = 0;
            if (!ParseLong(argv[++i], s)) {
                PrintUsage(argv[0]);
                return 1;
            }
            seed = static_cast<unsigned>(s);
        } else {
            PrintUsage(argv[0]);
            return 1;
        }
    }
    std::mt19937 rng(seed);

    Say("запущен, pid хоста " + std::to_string(static_cast<long>(hostPid)) + ", знакомлюсь с хостом...");
    std::string err;
    const int id = ClientHandshake(hostPid, IoTimeoutMs(), err);
    if (id <= 0) {
        Say("ошибка знакомства: " + err);
        return 1;
    }
    Say("знакомство выполнено, мой номер в игре: " + std::to_string(id));

    std::unique_ptr<Conn> conn;
    try {
        conn.reset(new Conn(ConnId{id, hostPid}, false));
    } catch (const std::exception& e) {
        Say(std::string("не удалось открыть соединение: ") + e.what());
        return 1;
    }

    Message hello = MakeMessage(MSG_HELLO, static_cast<int32_t>(getpid()));
    if (!conn->Write(&hello, sizeof(hello))) {
        Say("не удалось отправить приветствие: " + conn->LastError());
        return 2;
    }
    Say("присоединился к игре");

    bool alive = true;
    for (;;) {
        Message m;
        if (!conn->Read(&m, sizeof(m))) {
            if (g_stop) break;
            Say("связь с хостом потеряна: " + conn->LastError() + " - останавливаюсь");
            return 2;
        }
        if (g_stop) break;

        switch (m.type) {
            case MSG_PING:
                break;  // хост жив, ждём ход
            case MSG_REQUEST: {
                int number = fixed ? (alive ? fixedAlive : fixedDead)
                                   : std::uniform_int_distribution<int>(
                                         rules::kMinNumber, alive ? rules::kMaxAliveKidNumber : rules::kMaxDeadKidNumber)(rng);
                Message reply = MakeMessage(MSG_NUMBER, number);
                if (!conn->Write(&reply, sizeof(reply))) {
                    Say("не удалось отправить число: " + conn->LastError() + " - останавливаюсь");
                    return 2;
                }
                Say(std::string("бросил число ") + std::to_string(number) + (alive ? " (жив)" : " (мертв)"));
                break;
            }
            case MSG_STATUS:
                alive = (m.value == kStatusAlive);
                Say(std::string("статус от волка: ") + (alive ? "жив" : "мертв"));
                break;
            case MSG_GAME_OVER:
                Say("игра окончена, завершаюсь");
                return 0;
            default:
                Say("получено неизвестное сообщение " + std::to_string(m.type) + " - останавливаюсь");
                return 2;
        }
    }

    // Остановлен пользователем (SIGINT/SIGTERM): вежливо прощаемся
    Message bye = MakeMessage(MSG_BYE);
    conn->Write(&bye, sizeof(bye));
    Say("остановлен пользователем, завершаюсь");
    return 0;
}
