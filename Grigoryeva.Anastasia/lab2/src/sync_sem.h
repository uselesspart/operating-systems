// Синхронизация чтения/записи соединения глобальными (именованными, sem_open) семафорами.
// Для каждого соединения создаётся по семафору на каждое направление:
//   h2c - "данные от хоста клиенту готовы", c2h - "данные от клиента хосту готовы".
// Писатель: записал данные -> sem_post. Читатель: sem_timedwait (тайм-аут 5 c) -> прочитал данные.
#pragma once

#include <semaphore.h>

#include <string>

#include "conn.h"

// Уникальное имя соединения: "wolf_<pid хоста>_<id клиента>"
std::string ConnName(const ConnId& id);

class SemChannel {
public:
    // create = true - хост создаёт семафоры (sem_open + O_CREAT|O_EXCL), false - клиент открывает.
    // Бросает std::runtime_error при ошибке.
    SemChannel(const ConnId& id, bool create);
    ~SemChannel();
    SemChannel(const SemChannel&) = delete;
    SemChannel& operator=(const SemChannel&) = delete;

    void PostOutgoing();                                  // "в моём направлении появились данные"
    bool WaitIncoming(int timeoutMs, std::string& err);   // ждать данные от собеседника (с тайм-аутом)

private:
    sem_t* in_ = SEM_FAILED;
    sem_t* out_ = SEM_FAILED;
    std::string nameIn_;
    std::string nameOut_;
    bool owner_;
};
