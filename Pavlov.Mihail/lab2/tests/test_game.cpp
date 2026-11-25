#include "game.hpp"

#include <cstdlib>
#include <iostream>
#include <stdexcept>

namespace {

void check(bool condition, const char* message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void testRules()
{
    wolf::Game game;
    auto round = game.play(100, 30);
    check(round.hidden() == 1 && round.dead() == 0, "Разница 70: козлёнок должен спрятаться");

    round = game.play(100, 29);
    check(round.caught() == 1 && round.dead() == 1 && !round.finished,
          "Первая гибель не завершает игру");
    round = game.play(60, 40);
    check(round.alive && !round.wasAlive && !round.finished && round.hidden() == 0,
          "Разница 20: козлёнок должен воскреснуть");

    round = game.play(1, 100);
    check(round.caught() == 1 && !round.finished, "После воскрешения отсчёт гибели начинается заново");
    round = game.play(71, 50);
    check(round.finished && round.dead() == 1 && round.caught() == 0,
          "Два раунда подряд без живых козлят завершают игру");
}

void testInput()
{
    wolf::Game game;
    try {
        game.play(0, 50);
        throw std::runtime_error("Принято число волка вне диапазона");
    } catch (const std::invalid_argument&) {
    }

    game.play(100, 1);
    try {
        game.play(50, 51);
        throw std::runtime_error("Принято число мёртвого козлёнка вне диапазона");
    } catch (const std::invalid_argument&) {
    }
}

void testSession()
{
    wolf::GameSession session;
    for (int i = 0; i < 20; ++i) {
        // С числом 30 козлёнок прячется при любом броске от 1 до 100.
        const auto round = session.play(30);
        check(round.wolfNumber == 30 && round.kidNumber >= 1 && round.kidNumber <= 100 &&
                  round.wasAlive && round.alive && !round.finished,
              "Хост и дочерний процесс не согласовали результат раунда");
    }
}

}

int main()
{
    try {
        testRules();
        testInput();
        testSession();
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return EXIT_FAILURE;
    }
}
