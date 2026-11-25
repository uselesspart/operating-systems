#pragma once

#include <semaphore.h>
#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <functional>

constexpr int kTimeoutSec = 5;
constexpr int kWolfInputMs = 3000;

struct Shared {
    sem_t hostCanRead;
    sem_t goatCanRead;
};

struct GoatMsg {
    int32_t number;
};

struct WolfMsg {
    int32_t alive;
    int32_t gameOver;
};

Shared* createShared();
void destroyShared(Shared* shared);

bool waitSem(sem_t* sem, int seconds = kTimeoutSec);

bool retryWithTimeout(const std::function<bool()>& fn);

bool readAll(int fd, void* buf, size_t count);
bool writeAll(int fd, const void* buf, size_t count);

void logMsg(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void setLogName(const char* name);
