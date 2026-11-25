#include <cstdio>
#include <stdexcept>

#include "game.h"

namespace {

int failures = 0;

void check(bool condition, const char* what) {
    std::printf("%s %s\n", condition ? "[ OK ]" : "[FAIL]", what);
    if (!condition) ++failures;
}

}

int main() {
    {
        Game game(1);
        RoundResult r = game.play(20, {100});
        check(r.caught == 1 && !game.alive(0), "alive goat farther than 70 is caught");
    }
    {
        Game game(1);
        RoundResult r = game.play(50, {100});
        check(r.hidden == 1 && game.alive(0), "alive goat at distance exactly 70 hides");
    }
    {
        Game game(1);
        game.play(1, {100});
        RoundResult r = game.play(30, {50});
        check(r.revived == 1 && game.alive(0), "dead goat at distance 20 revives");
    }
    {
        Game game(1);
        game.play(1, {100});
        RoundResult r = game.play(1, {50});
        check(r.revived == 0 && !game.alive(0) && r.dead == 1, "dead goat farther than 20 stays dead");
    }
    {
        Game game(1);
        game.play(1, {100});
        check(!game.over(), "one dead round does not end the game");
        game.play(100, {1});
        check(game.over(), "two dead rounds in a row end the game");
    }
    {
        Game game(1);
        game.play(1, {100});
        game.play(30, {50});
        game.play(1, {100});
        check(!game.over(), "revival resets the dead streak");
    }
    {
        Game game(2);
        RoundResult r = game.play(50, {85, 86});
        check(r.hidden == 1 && r.caught == 1, "thresholds are divided by the number of goats (70/2 = 35)");
    }
    {
        bool threw = false;
        try {
            Game(1).play(1, {1, 2});
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        check(threw, "wrong number of goat numbers is rejected");
    }
    std::printf("%s\n", failures ? "game tests FAILED" : "game tests passed");
    return failures ? 1 : 0;
}
