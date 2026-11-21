#include "game_engine.hpp"

#include "protocol.hpp"

#include <cstdlib>
#include <stdexcept>

GameEngine::GameEngine() : alive_(true), dead_rounds_(0) {}

RoundResult GameEngine::ProcessRound(int wolf_number, int goat_number) {
    if (wolf_number < 1 || wolf_number > 100) {
        throw std::out_of_range("Wolf number must be in the range 1..100");
    }

    const int goat_maximum = alive_ ? 100 : 50;
    if (goat_number < 1 || goat_number > goat_maximum) {
        throw std::out_of_range("Goat number is outside the range allowed by its state");
    }

    const int distance = std::abs(wolf_number - goat_number);
    bool hidden = false;
    bool caught = false;
    bool revived = false;

    if (alive_) {
        if (distance <= 70 / kGoatCount) {
            hidden = true;
        } else {
            alive_ = false;
            caught = true;
        }
    } else if (distance <= 20 / kGoatCount) {
        alive_ = true;
        revived = true;
    }

    dead_rounds_ = alive_ ? 0 : dead_rounds_ + 1;

    return RoundResult{
        alive_,
        hidden,
        caught,
        revived,
        dead_rounds_ >= 2,
        alive_ ? 0 : kGoatCount
    };
}

bool GameEngine::IsAlive() const noexcept {
    return alive_;
}

int GameEngine::DeadRounds() const noexcept {
    return dead_rounds_;
}
