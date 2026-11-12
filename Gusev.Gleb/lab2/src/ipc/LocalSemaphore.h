#pragma once

#include <semaphore.h>

#include <chrono>

// Локальные (безымянные) семафоры POSIX: sem_init(pshared=1) в общей памяти,
// созданной до fork. Видны только хосту и его потомкам.
namespace wolf::sem {

sem_t* createShared(int count);
void destroyShared(sem_t* base, int count);
bool post(sem_t* s);
bool wait(sem_t* s, std::chrono::milliseconds timeout);

}
