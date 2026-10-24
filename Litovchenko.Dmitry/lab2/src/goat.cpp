#include "goat.h"

#include <exception>
#include <random>

#include "conn.h"

int runGoat(Shared* shared, int id, unsigned seed) {
    setLogName("goat");
    try {
        Conn conn(id, false);
        logMsg("подключился к волку (%s)", Conn::Type());
        std::mt19937 rng(seed);
        bool alive = true;
        for (;;) {
            GoatMsg msg{std::uniform_int_distribution<int>(1, alive ? 100 : 50)(rng)};
            if (!conn.Write(&msg, sizeof(msg))) {
                logMsg("не удалось отправить число, волк недоступен");
                return 1;
            }
            sem_post(&shared->hostCanRead);
            const int replyTimeout = kWolfInputMs / 1000 + kTimeoutSec;
            if (!waitSem(&shared->goatCanRead, replyTimeout)) {
                logMsg("волк не ответил за %d с (ввод числа + таймаут), завершаюсь", replyTimeout);
                return 2;
            }
            WolfMsg reply{};
            if (!conn.Read(&reply, sizeof(reply))) {
                logMsg("не удалось прочитать ответ волка");
                return 1;
            }
            if (reply.gameOver) {
                logMsg("игра окончена, завершаюсь");
                return 0;
            }
            alive = reply.alive;
        }
    } catch (const std::exception& e) {
        logMsg("ошибка: %s", e.what());
        return 1;
    }
}
