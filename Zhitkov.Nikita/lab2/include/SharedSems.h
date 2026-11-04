#pragma once
#include <semaphore.h>
#include <sys/mman.h>
#include <cerrno>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <string>

// Разделяемые данные хоста и клиента (живут в mmap MAP_SHARED, доступны обоим процессам после fork)
struct SharedSems {
    // Синхронизация очередности чтения/записи
    sem_t host_msg_ready;
    sem_t client_msg_ready;

    // Имя канала (mq/fifo), сформированное хостом в preFork()
    // Клиент читает отсюда - не нужны глобальные переменные
    char channel_name[128];

    // Пара fd socketpair, созданная в preFork() до fork
    // Хост забирает [0], клиент - [1]
    int sock_fds[2];
};

// Создаёт SharedSems в разделяемой памяти и инициализирует поля
// Семафоры с pshared=1 - видны другому процессу после fork
inline SharedSems* createSharedSems() {
    void* mem = mmap(nullptr, sizeof(SharedSems),
                     PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (mem == MAP_FAILED) {
        throw std::runtime_error(std::string("mmap: ") + std::strerror(errno));
    }
    auto* sems = static_cast<SharedSems*>(mem);
    sem_init(&sems->host_msg_ready, 1, 0);
    sem_init(&sems->client_msg_ready, 1, 0);
    sems->channel_name[0] = '\0';
    sems->sock_fds[0] = -1;
    sems->sock_fds[1] = -1;
    return sems;
}

// Ожидание семафора с таймаутом. Возвращает false, если не дождались (по требованию задания таймаут 5 секунд, при превышении - связь прерывается, клиент останавливается)
inline bool semWaitTimeout(sem_t* sem, int seconds = 5) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += seconds;
    while (sem_timedwait(sem, &ts) == -1) {
        if (errno == EINTR) continue;
        return false;
    }
    return true;
}