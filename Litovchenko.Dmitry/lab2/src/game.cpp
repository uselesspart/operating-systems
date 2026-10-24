#include "game.h"

#include <cstdlib>
#include <stdexcept>

Game::Game(int goats) : alive_(goats, true) {
    if (goats < 1) throw std::invalid_argument("at least one goat is needed");
}

RoundResult Game::play(int wolf, const std::vector<int>& numbers) {
    if (numbers.size() != alive_.size()) throw std::invalid_argument("one number per goat expected");

    const double n = goats();
    RoundResult r;
    r.round = ++round_;
    r.wolf = wolf;
    r.numbers = numbers;
    r.wasAlive = alive_;
    for (size_t i = 0; i < alive_.size(); ++i) {
        int diff = std::abs(numbers[i] - wolf);
        if (alive_[i]) {
            if (diff <= 70.0 / n) {
                ++r.hidden;
            } else {
                alive_[i] = false;
                ++r.caught;
            }
        } else if (diff <= 20.0 / n) {
            alive_[i] = true;
            ++r.revived;
        }
        if (!alive_[i]) ++r.dead;
    }
    r.alive = alive_;
    deadStreak_ = r.dead == goats() ? deadStreak_ + 1 : 0;
    r.gameOver = over();
    return r;
}
