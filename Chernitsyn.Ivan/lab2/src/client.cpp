#include CONN_HEADER
#include "game.h"

#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    char *end;
    long host = argc == 2 ? strtol(argv[1], &end, 10) : 0;
    if (host <= 0 || host > INT_MAX || *end) {
        fprintf(stderr, "Использование: %s PID_хоста\n", argv[0]);
        return 1;
    }

    Session session;
    Conn conn;
    if (!session.open(host, false) || !conn.open(session, false) || !session.claim()) {
        perror("Подключение");
        return 1;
    }
    if (kill(host, SIGUSR1) < 0) {
        perror("SIGUSR1");
        return 1;
    }

    srand(time(nullptr) ^ getpid());
    int state = 1;
    while (state != -1) {
        int goat = roll(state ? 100 : 50);
        if (!conn.write(goat) || sem_post(session.request) < 0 ||
            !wait_five_seconds(session.reply) || !conn.read(&state)) {
            perror("Обмен с хостом");
            return 1;
        }
        if (state < -1 || state > 1) {
            fprintf(stderr, "Неизвестный ответ хоста: %d\n", state);
            return 1;
        }
        printf("Козлёнок: %d — %s\n", goat, state == 1 ? "жив" : "мёртв");
        fflush(stdout);
    }
    puts("Игра окончена");
    return 0;
}
