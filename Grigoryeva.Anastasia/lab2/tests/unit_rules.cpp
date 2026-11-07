// Модульные тесты правил игры (без внешних библиотек).
#include <iostream>

#include "game_rules.h"

using namespace rules;

static int g_total = 0, g_failed = 0;
#define CHECK(cond)                                                                 \
    do {                                                                            \
        ++g_total;                                                                  \
        if (!(cond)) {                                                              \
            ++g_failed;                                                             \
            std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond "\n"; \
        }                                                                           \
    } while (0)

static void testDistances() {
    CHECK(HideDistance(1) == 70.0);
    CHECK(HideDistance(7) == 10.0);
    CHECK(ReviveDistance(1) == 20.0);
    CHECK(ReviveDistance(4) == 5.0);
    CHECK(HideDistance(0) == 70.0);  // защита от деления на ноль
}

static void testValidNumbers() {
    CHECK(IsValidKidNumber(true, 1));
    CHECK(IsValidKidNumber(true, 100));
    CHECK(!IsValidKidNumber(true, 0));
    CHECK(!IsValidKidNumber(true, 101));
    CHECK(IsValidKidNumber(false, 50));
    CHECK(!IsValidKidNumber(false, 51));  // мёртвые бросают только 1..50
    CHECK(!IsValidKidNumber(false, -3));
}

static void testAliveKid() {
    // n = 1: прячется при |разница| <= 70
    CHECK(Judge(true, 71, 1, 1) == Outcome::Hidden);   // ровно 70
    CHECK(Judge(true, 72, 1, 1) == Outcome::Caught);   // 71
    // n = 3: порог 23.33
    CHECK(Judge(true, 24, 1, 3) == Outcome::Hidden);   // 23
    CHECK(Judge(true, 25, 1, 3) == Outcome::Caught);   // 24
    // число совпало с волком
    CHECK(Judge(true, 50, 50, 7) == Outcome::Hidden);
    // симметрично в обе стороны
    CHECK(Judge(true, 1, 24, 3) == Outcome::Hidden);
    CHECK(Judge(true, 1, 25, 3) == Outcome::Caught);
}

static void testDeadKid() {
    // n = 1: воскресает при |разница| <= 20
    CHECK(Judge(false, 21, 1, 1) == Outcome::Revived);     // ровно 20
    CHECK(Judge(false, 22, 1, 1) == Outcome::StayedDead);  // 21
    // n = 3: порог 6.67
    CHECK(Judge(false, 7, 1, 3) == Outcome::Revived);      // 6
    CHECK(Judge(false, 8, 1, 3) == Outcome::StayedDead);   // 7
    // волк загадал большое число, мёртвый (1..50) далеко
    CHECK(Judge(false, 50, 100, 1) == Outcome::StayedDead);
}

static void testOutcomeHelpers() {
    CHECK(OutcomeIsAlive(Outcome::Hidden));
    CHECK(OutcomeIsAlive(Outcome::Revived));
    CHECK(!OutcomeIsAlive(Outcome::Caught));
    CHECK(!OutcomeIsAlive(Outcome::StayedDead));
    CHECK(std::string(OutcomeName(Outcome::Caught)) == "попался");
    CHECK(std::string(OutcomeName(Outcome::Hidden)) == "спрятался");
    CHECK(kAllDeadRoundsToFinish == 2);
}

int main() {
    testDistances();
    testValidNumbers();
    testAliveKid();
    testDeadKid();
    testOutcomeHelpers();
    std::cout << "unit_rules: проверок " << g_total << ", провалено " << g_failed << "\n";
    return g_failed == 0 ? 0 : 1;
}
