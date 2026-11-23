#include "kid.h"

#include <unistd.h>

#include <random>

#include "logger.h"
#include "protocol.h"

using namespace proto;

int RunKid(Conn& conn) {
    std::mt19937 rng(std::random_device{}() ^ static_cast<unsigned>(getpid()));
    auto rnd = [&rng](int lo, int hi) { return std::uniform_int_distribution<int>(lo, hi)(rng); };

    Log("kid", "Козлёнок запущен, подключаюсь к волку");
    Msg msg{kJoin, 0};
    if (!conn.Write(&msg, sizeof(msg))) {
        Log("kid", "Не удалось отправить запрос на подключение. Завершаюсь");
        return 1;
    }
    Msg reply{};
    if (!conn.Read(&reply, sizeof(reply)) || reply.type != kStatus) {
        Log("kid", "Волк не ответил на подключение. Завершаюсь");
        return 1;
    }
    bool alive = reply.value != 0;
    Log("kid", "Подключился к игре, статус: %s", alive ? "жив" : "мёртв");

    for (;;) {
        usleep(800 * 1000);
        const int number = alive ? rnd(1, kKidMaxAlive) : rnd(1, kKidMaxDead);
        msg = Msg{kNumber, number};
        if (!conn.Write(&msg, sizeof(msg))) {
            Log("kid", "Связь прервана при отправке числа. Завершаюсь");
            return 1;
        }
        if (!conn.Read(&reply, sizeof(reply)) ||
            (reply.type != kStatus && reply.type != kGameOver)) {
            Log("kid", "Связь прервана при ожидании статуса. Завершаюсь");
            return 1;
        }
        alive = reply.value != 0;
        Log("kid", "Выбросил %d, статус: %s", number, alive ? "жив" : "мёртв");
        if (reply.type == kGameOver) {
            Log("kid", "Игра окончена");
            return 0;
        }
    }
}
