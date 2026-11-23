#include "logger.h"

#include <cstdarg>
#include <cstdio>
#include <ctime>
#include <unistd.h>

static void Print(FILE* f, const char* ts, const char* tag, const char* msg) {
    fprintf(f, "[%s] [pid %d] [%s] %s\n", ts, static_cast<int>(getpid()), tag, msg);
}

void Log(const char* tag, const char* fmt, ...) {
    char msg[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);

    char ts[32];
    time_t t = time(nullptr);
    tm tmv;
    localtime_r(&t, &tmv);
    strftime(ts, sizeof(ts), "%H:%M:%S", &tmv);

    Print(stderr, ts, tag, msg);
    if (FILE* f = fopen("wolf_game.log", "a")) {
        Print(f, ts, tag, msg);
        fclose(f);
    }
}
