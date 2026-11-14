#include "game.hpp"
#include <cstdlib>
#include <stdexcept>

namespace lab {
Round Game::play(int wolf, int goat) {
    if (finished()) throw std::logic_error("Game has already finished");
    if (wolf < 1 || wolf > 100 || goat < 1 || goat > (alive_ ? 100 : 50))
        throw std::invalid_argument("Number outside the allowed range");
    const bool before = alive_;
    // Variant 1 has one client, so n = 1, thresholds are 70 and 20.
    alive_ = std::abs(wolf - goat) <= (before ? 70 : 20);
    deadStreak_ = alive_ ? 0 : deadStreak_ + 1;
    return {++round_, wolf, goat, before, alive_, before && alive_ ? 1 : 0,
            before && !alive_ ? 1 : 0, !before && alive_ ? 1 : 0,
            alive_ ? 0 : 1, finished()};
}
}
