#include "game/game_engine.hpp"

#include <gtest/gtest.h>
#include <stdexcept>
#include <vector>

namespace lab2::game {
namespace {

TEST(GameEngineTest, InitialState) {
    GameEngine game;

    EXPECT_EQ(game.clientCount(), 0U);
    EXPECT_FALSE(game.isRoundActive());
    EXPECT_FALSE(game.isGameOver());
    EXPECT_EQ(game.consecutiveAllDeadRounds(), 0U);
}

TEST(GameEngineTest, NewClientIsAlive) {
    GameEngine game;
    game.addClient(1);

    EXPECT_TRUE(game.hasClient(1));
    EXPECT_EQ(game.clientCount(), 1U);
    EXPECT_EQ(game.clientState(1), KidState::Alive);
}

TEST(GameEngineTest, RejectsDuplicateClient) {
    GameEngine game;
    game.addClient(1);

    EXPECT_THROW(game.addClient(1), std::invalid_argument);

    EXPECT_EQ(game.clientCount(), 1U);
}

TEST(GameEngineTest, RejectsZeroClientId) {
    GameEngine game;

    EXPECT_THROW(game.addClient(0), std::invalid_argument);

    EXPECT_EQ(game.clientCount(), 0U);
}

TEST(GameEngineTest, RemovesClient) {
    GameEngine game;
    game.addClient(1);

    EXPECT_TRUE(game.removeClient(1));
    EXPECT_FALSE(game.hasClient(1));
    EXPECT_EQ(game.clientCount(), 0U);

    EXPECT_FALSE(game.removeClient(1));
}

TEST(GameEngineTest, RejectsUnknownClientStateRequest) {
    GameEngine game;

    EXPECT_THROW(static_cast<void>(game.clientState(1)), std::out_of_range);
}

TEST(GameEngineTest, CannotStartRoundWithoutClients) {
    GameEngine game;

    EXPECT_THROW(game.beginRound(), std::logic_error);

    EXPECT_FALSE(game.isRoundActive());
}

TEST(GameEngineTest, CannotStartSecondActiveRound) {
    GameEngine game;
    game.addClient(1);

    game.beginRound();

    EXPECT_THROW(game.beginRound(), std::logic_error);

    EXPECT_TRUE(game.isRoundActive());
}

TEST(GameEngineTest, CannotFinishInactiveRound) {
    GameEngine game;

    EXPECT_THROW(game.finishRound(50, {}), std::logic_error);
}

TEST(GameEngineTest, AliveKidHidesAtExactThreshold) {
    GameEngine game;
    game.addClient(1);
    game.addClient(2);

    game.beginRound();

    // 70 / 2 = 35.
    const auto result = game.finishRound(50, {{1, 85}, {2, 86}});

    ASSERT_EQ(result.kids.size(), 2U);

    EXPECT_EQ(result.kids[0].outcome, KidOutcome::Hidden);
    EXPECT_EQ(result.kids[1].outcome, KidOutcome::Caught);

    EXPECT_EQ(game.clientState(1), KidState::Alive);
    EXPECT_EQ(game.clientState(2), KidState::Dead);

    EXPECT_EQ(result.hiddenCount, 1U);
    EXPECT_EQ(result.caughtCount, 1U);
    EXPECT_EQ(result.aliveCount, 1U);
    EXPECT_EQ(result.deadCount, 1U);
}

TEST(GameEngineTest, HandlesFractionalHideThreshold) {
    GameEngine game;

    game.addClient(1);
    game.addClient(2);
    game.addClient(3);

    game.beginRound();

    // 70 / 3 = 23.333...
    const auto result = game.finishRound(50, {{1, 73}, {2, 74}, {3, 27}});

    ASSERT_EQ(result.kids.size(), 3U);

    EXPECT_EQ(result.kids[0].outcome, KidOutcome::Hidden);
    EXPECT_EQ(result.kids[1].outcome, KidOutcome::Caught);
    EXPECT_EQ(result.kids[2].outcome, KidOutcome::Hidden);

    EXPECT_EQ(result.hiddenCount, 2U);
    EXPECT_EQ(result.caughtCount, 1U);
}

TEST(GameEngineTest, DeadKidRevivesAtExactThreshold) {
    GameEngine game;

    game.addClient(1);
    game.addClient(2);

    game.beginRound();
    game.finishRound(100, {{1, 1}, {2, 1}});

    ASSERT_EQ(game.clientState(1), KidState::Dead);
    ASSERT_EQ(game.clientState(2), KidState::Dead);

    game.beginRound();

    // 20 / 2 = 10.
    const auto result = game.finishRound(30, {{1, 40}, {2, 41}});

    ASSERT_EQ(result.kids.size(), 2U);

    EXPECT_EQ(result.kids[0].outcome, KidOutcome::Revived);
    EXPECT_EQ(result.kids[1].outcome, KidOutcome::RemainedDead);

    EXPECT_EQ(result.revivedCount, 1U);
    EXPECT_EQ(result.aliveCount, 1U);
    EXPECT_EQ(result.deadCount, 1U);

    EXPECT_FALSE(result.gameOver);
}

TEST(GameEngineTest, HandlesFractionalReviveThreshold) {
    GameEngine game;

    game.addClient(1);
    game.addClient(2);
    game.addClient(3);

    game.beginRound();
    game.finishRound(100, {{1, 1}, {2, 1}, {3, 1}});

    game.beginRound();

    // 20 / 3 = 6.666...
    const auto result = game.finishRound(30, {{1, 36}, {2, 37}, {3, 24}});

    ASSERT_EQ(result.kids.size(), 3U);

    EXPECT_EQ(result.kids[0].outcome, KidOutcome::Revived);
    EXPECT_EQ(result.kids[1].outcome, KidOutcome::RemainedDead);
    EXPECT_EQ(result.kids[2].outcome, KidOutcome::Revived);

    EXPECT_EQ(result.revivedCount, 2U);
    EXPECT_EQ(result.deadCount, 1U);
}

TEST(GameEngineTest, FirstAllDeadRoundDoesNotEndGame) {
    GameEngine game;
    game.addClient(1);

    game.beginRound();

    const auto result = game.finishRound(100, {{1, 1}});

    EXPECT_EQ(result.aliveCount, 0U);
    EXPECT_EQ(result.deadCount, 1U);
    EXPECT_EQ(result.consecutiveAllDeadRounds, 1U);

    EXPECT_FALSE(result.gameOver);
    EXPECT_FALSE(game.isGameOver());
}

TEST(GameEngineTest, SecondConsecutiveAllDeadRoundEndsGame) {
    GameEngine game;
    game.addClient(1);

    game.beginRound();
    game.finishRound(100, {{1, 1}});

    game.beginRound();

    const auto result = game.finishRound(100, {{1, 1}});

    EXPECT_EQ(result.consecutiveAllDeadRounds, 2U);
    EXPECT_TRUE(result.gameOver);
    EXPECT_TRUE(game.isGameOver());
    EXPECT_FALSE(game.isRoundActive());
}

TEST(GameEngineTest, RevivalResetsAllDeadCounter) {
    GameEngine game;
    game.addClient(1);

    game.beginRound();
    game.finishRound(100, {{1, 1}});

    ASSERT_EQ(game.consecutiveAllDeadRounds(), 1U);

    game.beginRound();

    const auto result = game.finishRound(10, {{1, 10}});

    EXPECT_EQ(result.revivedCount, 1U);
    EXPECT_EQ(result.consecutiveAllDeadRounds, 0U);
    EXPECT_FALSE(result.gameOver);

    game.beginRound();

    const auto nextResult = game.finishRound(100, {{1, 1}});

    EXPECT_EQ(nextResult.consecutiveAllDeadRounds, 1U);
    EXPECT_FALSE(nextResult.gameOver);
}

TEST(GameEngineTest, RejectsOperationsAfterGameOver) {
    GameEngine game;
    game.addClient(1);

    game.beginRound();
    game.finishRound(100, {{1, 1}});

    game.beginRound();
    game.finishRound(100, {{1, 1}});

    ASSERT_TRUE(game.isGameOver());

    EXPECT_THROW(game.beginRound(), std::logic_error);

    EXPECT_THROW(game.addClient(2), std::logic_error);
}

TEST(GameEngineTest, NewClientJoinsNextRound) {
    GameEngine game;
    game.addClient(1);

    const auto firstParticipants = game.beginRound();

    game.addClient(2);

    EXPECT_EQ(firstParticipants, (std::vector<ClientId>{1}));
    EXPECT_EQ(game.clientCount(), 2U);

    const auto firstResult = game.finishRound(50, {{1, 50}});

    ASSERT_EQ(firstResult.kids.size(), 1U);
    EXPECT_EQ(firstResult.initialKidCount, 1U);
    EXPECT_EQ(game.clientState(2), KidState::Alive);

    const auto nextParticipants = game.beginRound();

    EXPECT_EQ(nextParticipants, (std::vector<ClientId>{1, 2}));
}

TEST(GameEngineTest, DisconnectDoesNotChangeCurrentThreshold) {
    GameEngine game;

    game.addClient(1);
    game.addClient(2);

    game.beginRound();

    ASSERT_TRUE(game.removeClient(2));

    // При n = 2 порог равен 35.
    // При пересчёте n до 1 порог ошибочно стал бы равен 70.
    const auto result = game.finishRound(50, {{1, 90}});

    EXPECT_EQ(result.initialKidCount, 2U);
    ASSERT_EQ(result.kids.size(), 1U);

    EXPECT_EQ(result.caughtCount, 1U);
    EXPECT_EQ(game.clientState(1), KidState::Dead);
    EXPECT_EQ(game.clientCount(), 1U);
}

TEST(GameEngineTest, AllDisconnectedClientsCancelRound) {
    GameEngine game;
    game.addClient(1);

    game.beginRound();
    game.finishRound(100, {{1, 1}});

    ASSERT_EQ(game.consecutiveAllDeadRounds(), 1U);

    game.beginRound();
    game.removeClient(1);

    const auto result = game.finishRound(50, {});

    EXPECT_FALSE(result.completed);
    EXPECT_FALSE(result.gameOver);
    EXPECT_FALSE(game.isRoundActive());

    EXPECT_EQ(result.consecutiveAllDeadRounds, 0U);
    EXPECT_EQ(game.clientCount(), 0U);

    game.addClient(2);

    EXPECT_NO_THROW(game.beginRound());
}

TEST(GameEngineTest, MembershipChangeDoesNotResetCounter) {
    GameEngine game;

    game.addClient(1);
    game.addClient(2);

    game.beginRound();
    game.finishRound(100, {{1, 1}, {2, 1}});

    ASSERT_EQ(game.consecutiveAllDeadRounds(), 1U);

    game.removeClient(2);
    game.addClient(3);

    EXPECT_EQ(game.consecutiveAllDeadRounds(), 1U);

    game.beginRound();

    const auto result = game.finishRound(100, {{1, 1}, {3, 1}});

    EXPECT_EQ(result.consecutiveAllDeadRounds, 2U);
    EXPECT_TRUE(result.gameOver);
}

TEST(GameEngineTest, RejectsInvalidWolfNumbers) {
    GameEngine game;
    game.addClient(1);
    game.beginRound();

    EXPECT_THROW(game.finishRound(0, {{1, 50}}), std::invalid_argument);

    EXPECT_THROW(game.finishRound(101, {{1, 50}}), std::invalid_argument);

    EXPECT_TRUE(game.isRoundActive());
    EXPECT_EQ(game.clientState(1), KidState::Alive);
}

TEST(GameEngineTest, RejectsInvalidAliveKidNumbers) {
    GameEngine game;
    game.addClient(1);
    game.beginRound();

    EXPECT_THROW(game.finishRound(50, {{1, 0}}), std::invalid_argument);

    EXPECT_THROW(game.finishRound(50, {{1, 101}}), std::invalid_argument);

    EXPECT_TRUE(game.isRoundActive());
}

TEST(GameEngineTest, RejectsInvalidDeadKidNumber) {
    GameEngine game;
    game.addClient(1);

    game.beginRound();
    game.finishRound(100, {{1, 1}});

    game.beginRound();

    EXPECT_THROW(game.finishRound(50, {{1, 51}}), std::invalid_argument);

    EXPECT_EQ(game.clientState(1), KidState::Dead);
    EXPECT_TRUE(game.isRoundActive());
}

TEST(GameEngineTest, RejectsMissingAndUnexpectedMoves) {
    GameEngine game;

    game.addClient(1);
    game.addClient(2);

    game.beginRound();

    EXPECT_THROW(game.finishRound(50, {{1, 50}}), std::invalid_argument);

    EXPECT_THROW(game.finishRound(50, {{1, 50}, {3, 50}}), std::invalid_argument);

    EXPECT_THROW(game.finishRound(50, {{1, 50}, {2, 50}, {3, 50}}), std::invalid_argument);

    EXPECT_TRUE(game.isRoundActive());
}

TEST(GameEngineTest, InvalidMoveDoesNotPartiallyUpdateRound) {
    GameEngine game;

    game.addClient(1);
    game.addClient(2);

    game.beginRound();

    EXPECT_THROW(game.finishRound(100, {{1, 1}, {2, 101}}), std::invalid_argument);

    EXPECT_EQ(game.clientState(1), KidState::Alive);
    EXPECT_EQ(game.clientState(2), KidState::Alive);
    EXPECT_TRUE(game.isRoundActive());

    const auto result = game.finishRound(100, {{1, 1}, {2, 100}});

    EXPECT_EQ(result.caughtCount, 1U);
    EXPECT_EQ(result.hiddenCount, 1U);
    EXPECT_TRUE(result.completed);
}

TEST(GameEngineTest, SupportsMoreThanSeventyClients) {
    GameEngine game;
    ClientMoves moves;

    for (ClientId id = 1; id <= 71; ++id) {
        game.addClient(id);
        moves.emplace(id, 50);
    }

    moves.at(71) = 51;

    game.beginRound();

    const auto result = game.finishRound(50, moves);

    EXPECT_EQ(result.initialKidCount, 71U);
    EXPECT_EQ(result.hiddenCount, 70U);
    EXPECT_EQ(result.caughtCount, 1U);
    EXPECT_EQ(result.aliveCount, 70U);
    EXPECT_EQ(result.deadCount, 1U);
}

TEST(GameEngineTest, RoundStatisticsAreConsistent) {
    GameEngine game;

    game.addClient(1);
    game.addClient(2);
    game.addClient(3);
    game.addClient(4);

    game.beginRound();

    game.finishRound(100, {{1, 1}, {2, 1}, {3, 100}, {4, 100}});

    game.beginRound();

    const auto result = game.finishRound(30, {{1, 30}, {2, 50}, {3, 30}, {4, 100}});

    EXPECT_EQ(result.hiddenCount, 1U);
    EXPECT_EQ(result.caughtCount, 1U);
    EXPECT_EQ(result.revivedCount, 1U);
    EXPECT_EQ(result.deadCount, 2U);
    EXPECT_EQ(result.aliveCount, 2U);

    EXPECT_EQ(result.aliveCount + result.deadCount, result.kids.size());

    EXPECT_EQ(result.roundNumber, 2U);
    EXPECT_EQ(result.wolfNumber, 30);
    EXPECT_TRUE(result.completed);
}

} // namespace
} // namespace lab2::game
