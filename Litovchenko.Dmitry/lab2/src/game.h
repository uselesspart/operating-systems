#pragma once

#include <vector>

struct RoundResult {
    int round = 0;
    int wolf = 0;
    bool wolfManual = false;
    std::vector<int> numbers;
    std::vector<bool> wasAlive;
    std::vector<bool> alive;
    int hidden = 0;
    int caught = 0;
    int revived = 0;
    int dead = 0;
    bool gameOver = false;
};

class Game {
public:
    explicit Game(int goats);

    RoundResult play(int wolf, const std::vector<int>& numbers);

    int goats() const { return static_cast<int>(alive_.size()); }
    bool alive(int goat) const { return alive_.at(goat); }
    bool over() const { return deadStreak_ >= 2; }

private:
    std::vector<bool> alive_;
    int round_ = 0;
    int deadStreak_ = 0;
};
