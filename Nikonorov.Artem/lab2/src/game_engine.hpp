#pragma once

struct RoundResult {
    bool alive;
    bool hidden;
    bool caught;
    bool revived;
    bool game_over;
    int dead_goats;
};

class GameEngine {
public:
    GameEngine();

    RoundResult ProcessRound(int wolf_number, int goat_number);
    bool IsAlive() const noexcept;
    int DeadRounds() const noexcept;

private:
    bool alive_;
    int dead_rounds_;
};
