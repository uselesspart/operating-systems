#pragma once

namespace wolf {

struct RoundOutcome {
    bool hidden = false;
    bool caught = false;
    bool revived = false;
    bool alive = true;
    bool game_over = false;
    int dead_goats = 0;
};

class Game {
public:
    RoundOutcome PlayRound(int wolf, int goat);

    bool Alive() const noexcept { return alive_; }
    int Round() const noexcept { return round_; }
    int DeadRounds() const noexcept { return dead_rounds_; }
    int HiddenTotal() const noexcept { return hidden_; }
    int CaughtTotal() const noexcept { return caught_; }
    int RevivedTotal() const noexcept { return revived_; }

private:
    bool alive_ = true;
    int round_ = 0;
    int dead_rounds_ = 0;
    int hidden_ = 0;
    int caught_ = 0;
    int revived_ = 0;
};

}
