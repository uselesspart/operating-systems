#pragma once

#include <semaphore.h>

namespace wolf {

class Conn;

// Параметры потомка: номер в сессии и зерно генератора для воспроизводимых бросков.
struct KidOptions {
    int id = 0;
    unsigned seed = 0;
};

// Цикл козлёнка внутри процесса-потомка. Возвращает код выхода:
// 0 — игра окончена или хост попрощался, 1 — ошибка или обрыв связи.
int runKid(const KidOptions& options, Conn& conn, sem_t* toKid, sem_t* toHost);

}
