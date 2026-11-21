#include "../src/game_engine.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>

namespace {

void Require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void TestAliveGoatHidesAtBoundary() {
    GameEngine game;
    const RoundResult result = game.ProcessRound(1, 71);
    Require(result.alive, "Goat should remain alive when the distance equals 70");
    Require(result.hidden, "Goat should hide at the 70-point boundary");
    Require(!result.caught && !result.revived && !result.game_over,
            "Unexpected flags for a hidden goat");
}

void TestAliveGoatIsCaught() {
    GameEngine game;
    const RoundResult result = game.ProcessRound(1, 100);
    Require(!result.alive && result.caught, "Goat should be caught when distance exceeds 70");
    Require(result.dead_goats == 1 && !result.game_over,
            "First dead round must not finish the game");
}

void TestDeadGoatRevivesAtBoundary() {
    GameEngine game;
    const RoundResult caught = game.ProcessRound(1, 100);
    Require(caught.caught, "Setup round should catch the goat");

    const RoundResult revived = game.ProcessRound(40, 20);
    Require(revived.alive && revived.revived, "Dead goat should revive at distance 20");
    Require(revived.dead_goats == 0 && !revived.game_over,
            "Revival must reset the consecutive-dead counter");
}

void TestGameEndsAfterTwoDeadRounds() {
    GameEngine game;
    const RoundResult first = game.ProcessRound(1, 100);
    Require(!first.game_over, "Game must continue after the first dead round");

    const RoundResult second = game.ProcessRound(100, 1);
    Require(!second.alive && second.game_over,
            "Game must end after two consecutive rounds with all goats dead");
}

void TestInvalidGoatRangeIsRejected() {
    GameEngine game;
    bool rejected = false;
    try {
        (void)game.ProcessRound(1, 101);
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    Require(rejected, "Alive goat number above 100 must be rejected");
}

void TestDeadGoatCannotThrowAbove50() {
    GameEngine game;
    (void)game.ProcessRound(1, 100);
    bool rejected = false;
    try {
        (void)game.ProcessRound(50, 51);
    } catch (const std::out_of_range&) {
        rejected = true;
    }
    Require(rejected, "Dead goat number above 50 must be rejected");
}

}

int main() {
    try {
        TestAliveGoatHidesAtBoundary();
        TestAliveGoatIsCaught();
        TestDeadGoatRevivesAtBoundary();
        TestGameEndsAfterTwoDeadRounds();
        TestInvalidGoatRangeIsRejected();
        TestDeadGoatCannotThrowAbove50();
        std::cout << "All game engine tests passed\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "Game engine test failed: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
