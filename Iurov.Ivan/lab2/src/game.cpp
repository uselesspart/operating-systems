#include "wolf/game.hpp"

#include "wolf/protocol.hpp"

#include <cstdlib>
#include <stdexcept>

namespace wolf {

RoundOutcome Game::PlayRound(int wolf, int goat) {
    if (wolf < 1 || wolf > 100) {
        throw std::out_of_range("число волка должно быть в диапазоне 1..100");
    }

    const int max_goat = alive_ ? 100 : 50;
    if (goat < 1 || goat > max_goat) {
        throw std::out_of_range("число козлёнка вне допустимого диапазона");
    }

    ++round_;
    const int distance = std::abs(wolf - goat);
    RoundOutcome outcome;

    if (alive_) {
        if (distance <= 70 / kGoatCount) {
            outcome.hidden = true;
            ++hidden_;
        } else {
            alive_ = false;
            outcome.caught = true;
            ++caught_;
        }
    } else if (distance <= 20 / kGoatCount) {
        alive_ = true;
        outcome.revived = true;
        ++revived_;
    }

    dead_rounds_ = alive_ ? 0 : dead_rounds_ + 1;
    outcome.alive = alive_;
    outcome.dead_goats = alive_ ? 0 : kGoatCount;
    outcome.game_over = dead_rounds_ >= 2;
    return outcome;
}

}
