#include "game.h"

#include <stdint.h>
#include <criterion/criterion.h>

Test(game, hiding_boundary)
{
    Game game = {true, 0, 0};
    play_round(&game, 100, 30);
    cr_assert(game.alive);
    play_round(&game, 100, 29);
    cr_assert_not(game.alive);
    cr_assert_eq(game.dead_rounds, 1);
}

Test(game, revival_breaks_dead_streak)
{
    Game game = {false, 1, 4};
    play_round(&game, 70, 50);
    cr_assert(game.alive);
    cr_assert_eq(game.dead_rounds, 0);
    cr_assert_eq(game.round, 5);
}

Test(game, second_dead_round_ends_game)
{
    Game game = {true, 0, 0};
    play_round(&game, 1, 100);
    play_round(&game, 1, 22);
    cr_assert_eq(game.dead_rounds, 2);
    cr_assert_not(game.alive);
}

Test(game, rolls_stay_in_range)
{
    for (int i = 0; i < 1000; ++i) {
        int live = roll(100);
        int dead = roll(50);
        cr_assert(live >= 1 && live <= 100);
        cr_assert(dead >= 1 && dead <= 50);
    }
}
