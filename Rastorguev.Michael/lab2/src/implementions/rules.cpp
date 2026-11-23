#include "rules.h"

#include <cstdlib>

#include "protocol.h"

namespace rules {

Outcome Judge(bool kidAlive, int kidNumber, int wolfNumber) {
    const int diff = std::abs(kidNumber - wolfNumber);
    if (kidAlive) return diff <= proto::kHideRange ? Outcome::kHidden : Outcome::kCaught;
    return diff <= proto::kReviveRange ? Outcome::kRevived : Outcome::kStillDead;
}

bool AliveAfter(Outcome outcome) {
    return outcome == Outcome::kHidden || outcome == Outcome::kRevived;
}

const char* OutcomeName(Outcome outcome) {
    switch (outcome) {
        case Outcome::kHidden: return "спрятался";
        case Outcome::kCaught: return "попался";
        case Outcome::kRevived: return "воскрес";
        case Outcome::kStillDead: return "остался мёртвым";
    }
    return "";
}

bool DeadStreak::Update(bool kidAlive) {
    count_ = kidAlive ? 0 : count_ + 1;
    return count_ >= proto::kMaxDeadRounds;
}

}
