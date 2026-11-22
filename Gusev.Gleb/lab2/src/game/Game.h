#pragma once

#include <map>
#include <vector>

#include "ipc/Protocol.h"

// Правила игры без ввода-вывода. n — число козлят в раунде:
// живой прячется при |козлёнок - волк| <= 70/n, иначе гибнет;
// мёртвый воскресает при |козлёнок - волк| <= 20/n.
// Игра окончена, когда все козлята мертвы два хода подряд.
namespace wolf::game {

inline constexpr int kWolfMax = 100;
inline constexpr int kAliveMax = 100;
inline constexpr int kDeadMax = 50;
inline constexpr int kAllDeadRoundsToEnd = 2;

int maxThrow(bool alive);
double hideRange(int kids);
double resurrectRange(int kids);

struct Throw {
    int kidId = 0;
    int number = 0;
};

struct KidResult {
    int kidId = 0;
    int number = 0;
    int diff = 0;
    bool wasAlive = false;
    bool aliveAfter = false;
    proto::Outcome outcome = proto::Outcome::None;
};

struct RoundResult {
    int round = 0;
    int wolf = 0;
    int kids = 0;
    double hideRange = 0;
    double resurrectRange = 0;
    std::vector<KidResult> results;
    int hidden = 0;
    int caught = 0;
    int resurrected = 0;
    int dead = 0;
    bool gameOver = false;
};

class Game {
public:
    void addKid(int kidId);
    void removeKid(int kidId);
    bool hasKid(int kidId) const;
    bool isAlive(int kidId) const;
    int kidCount() const { return static_cast<int>(alive_.size()); }
    int aliveCount() const;

    int round() const { return round_; }
    bool over() const { return allDeadStreak_ >= kAllDeadRoundsToEnd; }

    RoundResult playRound(int wolfNumber, const std::vector<Throw>& throws);

    void reset();
    void clear();

private:
    std::map<int, bool> alive_;
    int round_ = 0;
    int allDeadStreak_ = 0;
};

}
