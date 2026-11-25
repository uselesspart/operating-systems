#include "wolf/game.hpp"

#include <cstdlib>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {

void Require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Function>
void RequireOutOfRange(Function&& function, const std::string& message) {
    try {
        function();
    } catch (const std::out_of_range&) {
        return;
    }
    throw std::runtime_error(message);
}

void TestAliveGoatHidesAtBoundary() {
    wolf::Game game;
    const wolf::RoundOutcome outcome = game.PlayRound(1, 71);
    Require(outcome.hidden, "при расстоянии 70 живой козлёнок должен прятаться");
    Require(outcome.alive, "спрятавшийся козлёнок остаётся жив");
    Require(!outcome.caught && !outcome.revived, "лишние признаки раунда");
    Require(outcome.dead_goats == 0 && !outcome.game_over, "игра не должна завершиться");
    Require(game.HiddenTotal() == 1, "счётчик спрятавшихся должен увеличиться");
}

void TestAliveGoatIsCaught() {
    wolf::Game game;
    const wolf::RoundOutcome outcome = game.PlayRound(1, 72);
    Require(outcome.caught, "при расстоянии 71 козлёнок должен попасться");
    Require(!outcome.alive, "пойманный козлёнок мёртв");
    Require(outcome.dead_goats == 1 && !outcome.game_over,
            "после первого мёртвого раунда игра продолжается");
}

void TestDeadGoatRevivesAtBoundary() {
    wolf::Game game;
    Require(!game.PlayRound(1, 100).alive, "подготовка: козлёнок должен быть пойман");
    const wolf::RoundOutcome outcome = game.PlayRound(50, 30);
    Require(outcome.revived, "при расстоянии 20 мёртвый козлёнок воскресает");
    Require(outcome.alive && outcome.dead_goats == 0, "воскресший козлёнок жив");
    Require(game.DeadRounds() == 0, "воскрешение сбрасывает счётчик мёртвых раундов");
}

void TestGameEndsAfterTwoDeadRounds() {
    wolf::Game game;
    Require(!game.PlayRound(1, 100).game_over, "игра не завершается после одного раунда");
    const wolf::RoundOutcome second = game.PlayRound(100, 1);
    Require(second.game_over, "два мёртвых раунда подряд завершают игру");
    Require(game.DeadRounds() == 2, "счётчик мёртвых раундов равен двум");
}

void TestRevivalResetsDeadRounds() {
    wolf::Game game;
    Require(!game.PlayRound(1, 100).alive, "подготовка: козлёнок пойман");
    Require(game.PlayRound(50, 30).revived, "подготовка: козлёнок воскрес");
    const wolf::RoundOutcome outcome = game.PlayRound(1, 100);
    Require(!outcome.game_over, "после воскрешения нужен новый мёртвый раунд");
    Require(game.DeadRounds() == 1, "счётчик мёртвых раундов начинается заново");
}

void TestWolfNumberRange() {
    wolf::Game game;
    RequireOutOfRange([&game]() { game.PlayRound(0, 50); }, "число волка 0 должно быть отклонено");
    RequireOutOfRange([&game]() { game.PlayRound(101, 50); },
                      "число волка 101 должно быть отклонено");
}

void TestGoatNumberRange() {
    wolf::Game alive;
    RequireOutOfRange([&alive]() { alive.PlayRound(1, 101); },
                      "живой козлёнок не может выбросить больше 100");

    wolf::Game dead;
    Require(!dead.PlayRound(1, 100).alive, "подготовка: козлёнок должен быть пойман");
    RequireOutOfRange([&dead]() { dead.PlayRound(1, 51); },
                      "мёртвый козлёнок не может выбросить больше 50");
}

}

int main() {
    try {
        TestAliveGoatHidesAtBoundary();
        TestAliveGoatIsCaught();
        TestDeadGoatRevivesAtBoundary();
        TestGameEndsAfterTwoDeadRounds();
        TestRevivalResetsDeadRounds();
        TestWolfNumberRange();
        TestGoatNumberRange();
        std::cout << "тесты игровой логики пройдены\n";
        return EXIT_SUCCESS;
    } catch (const std::exception& exception) {
        std::cerr << "тест не пройден: " << exception.what() << '\n';
        return EXIT_FAILURE;
    }
}
