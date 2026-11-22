#include "game.hpp"
#include <iostream>
#include <stdexcept>

namespace {
void check(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
template<class Function> void mustThrow(Function function) {
    bool thrown = false;
    try { function(); } catch (const std::exception&) { thrown = true; }
    check(thrown, "Expected an exception");
}
}
int main() {
    try {
        lab::Game hidden;
        auto r = hidden.play(100, 30);
        check(r.alive && r.hidden == 1 && r.caught == 0 && r.dead == 0, "Threshold 70 must be inclusive");
        lab::Game resurrection;
        r = resurrection.play(100, 29);
        check(!r.alive && r.caught == 1 && r.dead == 1 && !r.finished, "Difference 71 must kill");
        r = resurrection.play(70, 50);
        check(r.alive && r.resurrected == 1 && r.hidden == 0 && !r.finished, "Threshold 20 must resurrect");
        r = resurrection.play(100, 1);
        check(!r.finished && r.caught == 1, "Resurrection must reset dead streak");
        r = resurrection.play(71, 50);
        check(r.finished && r.dead == 1 && r.caught == 0 && r.number == 4, "Difference 21 must keep dead and finish");
        mustThrow([&] { resurrection.play(1, 1); });
        for (int wolf = 1; wolf <= 100; ++wolf) {
            for (int goat = 1; goat <= 100; ++goat) {
                lab::Game game;
                const auto result = game.play(wolf, goat);
                check(result.alive == (std::abs(wolf - goat) <= 70), "Alive rules exhaustive check failed");
                check(result.hidden + result.caught == 1, "Live goat must hide or get caught");
            }
            for (int goat = 1; goat <= 50; ++goat) {
                lab::Game game;
                game.play(100, 1);
                const auto result = game.play(wolf, goat);
                check(result.alive == (std::abs(wolf - goat) <= 20), "Dead rules exhaustive check failed");
                check(result.finished == !result.alive, "Dead streak check failed");
            }
        }
        lab::Game validation;
        mustThrow([&] { validation.play(0, 1); });
        mustThrow([&] { validation.play(101, 1); });
        mustThrow([&] { validation.play(1, 0); });
        mustThrow([&] { validation.play(1, 101); });
        check(validation.round() == 0 && validation.alive(), "Invalid input mutated game");
        validation.play(100, 1);
        mustThrow([&] { validation.play(100, 51); });
        check(validation.round() == 1, "Invalid dead goat mutated game");
        std::cout << "Game: 15000 roll pairs, boundaries, resurrection, finish, invalid inputs: OK\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
