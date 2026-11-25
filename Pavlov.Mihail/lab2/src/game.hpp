#pragma once

#include <memory>

namespace wolf {

struct Round {
    int wolfNumber;
    int kidNumber;
    bool wasAlive;
    bool alive;
    bool finished;

    int hidden() const { return wasAlive && alive; }
    int caught() const { return wasAlive && !alive; }
    int dead() const { return !alive; }
};

class Game {
public:
    Round play(int wolfNumber, int kidNumber);

private:
    bool alive_ = true;
    int roundsAllDead_ = 0;
};

class GameSession {
public:
    GameSession();
    ~GameSession();

    GameSession(const GameSession&) = delete;
    GameSession& operator=(const GameSession&) = delete;

    Round play(int wolfNumber);

private:
    struct Process;
    std::unique_ptr<Process> process_;
    Game game_;
};

int rollWolfNumber();

}
