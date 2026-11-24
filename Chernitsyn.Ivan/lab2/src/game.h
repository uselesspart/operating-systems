#pragma once

struct Game {
    bool alive;
    int dead_rounds;
    int round;
};

void play_round(Game *game, int wolf, int goat);
int roll(int maximum);
