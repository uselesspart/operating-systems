#include "game/Game.h"

#include <cstdlib>

namespace wolf::game {

int maxThrow(bool alive) { return alive ? kAliveMax : kDeadMax; }

double hideRange(int kids) { return kids > 0 ? 70.0 / kids : 0.0; }
double resurrectRange(int kids) { return kids > 0 ? 20.0 / kids : 0.0; }

void Game::addKid(int kidId) { alive_[kidId] = true; }
void Game::removeKid(int kidId) { alive_.erase(kidId); }
bool Game::hasKid(int kidId) const { return alive_.count(kidId) != 0; }

bool Game::isAlive(int kidId) const {
    const auto it = alive_.find(kidId);
    return it != alive_.end() && it->second;
}

int Game::aliveCount() const {
    int n = 0;
    for (const auto& [id, alive] : alive_) n += alive ? 1 : 0;
    return n;
}

RoundResult Game::playRound(int wolfNumber, const std::vector<Throw>& throws) {
    RoundResult r;
    r.round = ++round_;
    r.wolf = wolfNumber;

    std::vector<Throw> valid;
    for (const auto& t : throws) {
        if (hasKid(t.kidId)) valid.push_back(t);
    }
    r.kids = static_cast<int>(valid.size());
    r.hideRange = hideRange(r.kids);
    r.resurrectRange = resurrectRange(r.kids);

    for (const auto& t : valid) {
        KidResult k;
        k.kidId = t.kidId;
        k.number = t.number;
        k.diff = std::abs(t.number - wolfNumber);
        k.wasAlive = isAlive(t.kidId);
        if (k.wasAlive) {
            const bool hid = k.diff <= r.hideRange;
            k.outcome = hid ? proto::Outcome::Hidden : proto::Outcome::Caught;
            k.aliveAfter = hid;
            (hid ? r.hidden : r.caught) += 1;
        } else {
            const bool back = k.diff <= r.resurrectRange;
            k.outcome = back ? proto::Outcome::Resurrected : proto::Outcome::StayedDead;
            k.aliveAfter = back;
            r.resurrected += back ? 1 : 0;
        }
        alive_[t.kidId] = k.aliveAfter;
        r.results.push_back(k);
    }

    r.dead = kidCount() - aliveCount();
    const bool allDead = kidCount() > 0 && aliveCount() == 0;
    allDeadStreak_ = allDead ? allDeadStreak_ + 1 : 0;
    r.gameOver = over();
    return r;
}

void Game::reset() {
    for (auto& [id, alive] : alive_) {
        (void)id;
        alive = true;
    }
    round_ = 0;
    allDeadStreak_ = 0;
}

void Game::clear() {
    alive_.clear();
    round_ = 0;
    allDeadStreak_ = 0;
}

}
