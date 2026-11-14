#pragma once

namespace lab {
struct Round {
    int number;
    int wolf;
    int goat;
    bool wasAlive;
    bool alive;
    int hidden;
    int caught;
    int resurrected;
    int dead;
    bool finished;
};
class Game {
public:
    Round play(int wolf, int goat);
    bool alive() const { return alive_; }
    bool finished() const { return deadStreak_ >= 2; }
    int round() const { return round_; }
private:
    bool alive_ = true;
    int deadStreak_ = 0;
    int round_ = 0;
};
}
