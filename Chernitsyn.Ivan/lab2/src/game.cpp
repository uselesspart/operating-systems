#include "game.h"

#include <stdlib.h>

void play_round(Game *game, int wolf, int goat)
{
    int limit = game->alive ? 70 : 20;
    game->alive = abs(wolf - goat) <= limit;
    game->dead_rounds = game->alive ? 0 : game->dead_rounds + 1;
    ++game->round;
}

int roll(int maximum)
{
    return 1 + rand() % maximum;
}
