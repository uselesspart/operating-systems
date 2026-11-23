#include <iostream>

#include "rules.h"

namespace {

const char* const VERDICT[] = {"FAILED", "PASSED"};

using rules::Outcome;

bool testAliveKidHidesUpToSeventy() {
    return rules::Judge(true, 10, 80) == Outcome::kHidden &&
           rules::Judge(true, 80, 10) == Outcome::kHidden;
}

bool testAliveKidIsCaughtBeyondSeventy() {
    return rules::Judge(true, 10, 81) == Outcome::kCaught;
}

bool testDeadKidRevivesUpToTwenty() {
    return rules::Judge(false, 30, 50) == Outcome::kRevived &&
           rules::Judge(false, 50, 30) == Outcome::kRevived;
}

bool testDeadKidStaysDeadBeyondTwenty() {
    return rules::Judge(false, 30, 51) == Outcome::kStillDead;
}

bool testAliveAfterOutcome() {
    return rules::AliveAfter(Outcome::kHidden) && rules::AliveAfter(Outcome::kRevived) &&
           !rules::AliveAfter(Outcome::kCaught) && !rules::AliveAfter(Outcome::kStillDead);
}

bool testGameEndsAfterTwoDeadRounds() {
    rules::DeadStreak streak;

    bool afterFirst = streak.Update(false);
    bool afterSecond = streak.Update(false);

    return !afterFirst && afterSecond;
}

bool testReviveResetsDeadStreak() {
    rules::DeadStreak streak;

    streak.Update(false);
    streak.Update(true);
    bool afterNewDeath = streak.Update(false);

    return !afterNewDeath;
}

bool run(const char* name, bool (*test)()) {
    std::cout << "[ Запуск    ] " << name << '\n';
    bool passed = false;
    try {
        passed = test();
    } catch (const std::exception& e) {
        std::cout << "  exception: " << e.what() << '\n';
    }
    std::cout << "[ " << VERDICT[passed] << " ] " << name << '\n';
    return passed;
}

}

int main() {
    const std::pair<const char*, bool (*)()> tests[] = {
        {"testAliveKidHidesUpToSeventy",         testAliveKidHidesUpToSeventy},
        {"testAliveKidIsCaughtBeyondSeventy",    testAliveKidIsCaughtBeyondSeventy},
        {"testDeadKidRevivesUpToTwenty",         testDeadKidRevivesUpToTwenty},
        {"testDeadKidStaysDeadBeyondTwenty",     testDeadKidStaysDeadBeyondTwenty},
        {"testAliveAfterOutcome",                testAliveAfterOutcome},
        {"testGameEndsAfterTwoDeadRounds",       testGameEndsAfterTwoDeadRounds},
        {"testReviveResetsDeadStreak",           testReviveResetsDeadStreak},
    };

    int failed = 0;
    for (const auto& [name, fn] : tests)
        failed += !run(name, fn);

    std::cout << "Провалено: " << failed << '\n';
    return failed;
}
