#include "game_rules.h"

#include <cstdlib>

namespace rules {

double HideDistance(int kids) { return 70.0 / (kids > 0 ? kids : 1); }
double ReviveDistance(int kids) { return 20.0 / (kids > 0 ? kids : 1); }

bool IsValidKidNumber(bool alive, int number) {
    return number >= kMinNumber && number <= (alive ? kMaxAliveKidNumber : kMaxDeadKidNumber);
}

Outcome Judge(bool alive, int kidNumber, int wolfNumber, int kids) {
    const double diff = static_cast<double>(std::abs(kidNumber - wolfNumber));
    if (alive) return diff <= HideDistance(kids) ? Outcome::Hidden : Outcome::Caught;
    return diff <= ReviveDistance(kids) ? Outcome::Revived : Outcome::StayedDead;
}

bool OutcomeIsAlive(Outcome o) { return o == Outcome::Hidden || o == Outcome::Revived; }

const char* OutcomeName(Outcome o) {
    switch (o) {
        case Outcome::Hidden: return "спрятался";
        case Outcome::Caught: return "попался";
        case Outcome::Revived: return "воскрес";
        case Outcome::StayedDead: return "остался мертвым";
    }
    return "?";
}

}  // namespace rules
