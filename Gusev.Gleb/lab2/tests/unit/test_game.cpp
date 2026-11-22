#include "game/Game.h"
#include "test.h"

TEST_SUITE("Game — правила «Волк и семеро козлят»");

using wolf::game::Game;
using wolf::game::RoundResult;
using wolf::game::Throw;
using wolf::proto::Outcome;

namespace wolf::proto {
static std::ostream& operator<<(std::ostream& os, Outcome o) { return os << outcomeName(o); }
}

namespace {

const wolf::game::KidResult* find(const RoundResult& r, int kid) {
    for (const auto& k : r.results) {
        if (k.kidId == kid) return &k;
    }
    return nullptr;
}

void kill(Game& g, int kid) { g.playRound(100, {{kid, 1}}); }

}

TEST(thresholds, "пороги 70/n и 20/n для разного числа козлят") {
    const struct { int n; double hide; double res; } cases[] = {
        {1, 70.0, 20.0}, {2, 35.0, 10.0}, {3, 70.0 / 3, 20.0 / 3}, {7, 10.0, 20.0 / 7}};
    for (const auto& c : cases) {
        CONTEXT("n = " << c.n);
        EXPECT_EQ(wolf::game::hideRange(c.n), c.hide);
        EXPECT_EQ(wolf::game::resurrectRange(c.n), c.res);
    }
    EXPECT_EQ(wolf::game::maxThrow(true), 100);
    EXPECT_EQ(wolf::game::maxThrow(false), 50);
}

TEST(alive_kid_hides_on_boundary, "живой: разница ровно 70/n — спрятался") {
    Game g;
    g.addKid(1);
    g.addKid(2);
    const auto r = g.playRound(50, {{1, 85}, {2, 15}});
    ASSERT_EQ(r.results.size(), 2u);
    EXPECT_EQ(find(r, 1)->outcome, Outcome::Hidden);
    EXPECT_EQ(find(r, 2)->outcome, Outcome::Hidden);
    EXPECT_EQ(r.hidden, 2);
    EXPECT_EQ(r.caught, 0);
    EXPECT_TRUE(g.isAlive(1) && g.isAlive(2));
}

TEST(alive_kid_caught, "живой: разница больше 70/n — попался и умер") {
    Game g;
    g.addKid(1);
    g.addKid(2);
    const auto r = g.playRound(50, {{1, 86}, {2, 50}});
    EXPECT_EQ(find(r, 1)->outcome, Outcome::Caught);
    EXPECT_FALSE(find(r, 1)->aliveAfter);
    EXPECT_FALSE(g.isAlive(1));
    EXPECT_EQ(find(r, 2)->outcome, Outcome::Hidden);
    EXPECT_EQ(r.caught, 1);
    EXPECT_EQ(r.dead, 1);
}

TEST(dead_kid_resurrects, "мёртвый: разница ≤ 20/n — воскрес, иначе остался мёртв") {
    Game g;
    g.addKid(1);
    g.addKid(2);
    g.playRound(100, {{1, 1}, {2, 1}});
    ASSERT_TRUE(!g.isAlive(1) && !g.isAlive(2));
    const auto r = g.playRound(40, {{1, 30}, {2, 29}});
    EXPECT_EQ(find(r, 1)->outcome, Outcome::Resurrected);
    EXPECT_EQ(find(r, 2)->outcome, Outcome::StayedDead);
    EXPECT_EQ(r.resurrected, 1);
    EXPECT_EQ(r.dead, 1);
    EXPECT_TRUE(g.isAlive(1));
    EXPECT_FALSE(g.isAlive(2));
}

TEST(n_changes_thresholds, "порог зависит от числа бросавших: n=1 и n=3") {
    Game one;
    one.addKid(1);
    EXPECT_EQ(one.playRound(50, {{1, 100}}).results[0].outcome, Outcome::Hidden);

    Game three;
    for (int id : {1, 2, 3}) three.addKid(id);
    const auto r = three.playRound(50, {{1, 100}, {2, 50}, {3, 50}});
    EXPECT_EQ(find(r, 1)->outcome, Outcome::Caught);
    EXPECT_EQ(r.kids, 3);
}

TEST(counts_are_consistent, "спрятались + попались = живых до хода; мертвы после хода") {
    Game g;
    for (int id = 1; id <= 4; ++id) g.addKid(id);
    g.playRound(100, {{1, 1}, {2, 99}, {3, 98}, {4, 97}});
    const auto r = g.playRound(50, {{1, 45}, {2, 60}, {3, 90}, {4, 50}});
    EXPECT_EQ(find(r, 1)->outcome, Outcome::Resurrected);
    EXPECT_EQ(r.hidden, 2);
    EXPECT_EQ(r.caught, 1);
    EXPECT_EQ(r.resurrected, 1);
    EXPECT_EQ(r.dead, 1);
    EXPECT_EQ(r.hidden + r.caught, 3);
}

TEST(game_over_after_two_all_dead_rounds, "игра заканчивается, когда все мертвы 2 хода подряд") {
    Game g;
    g.addKid(1);
    kill(g, 1);
    EXPECT_FALSE(g.over());
    const auto r = g.playRound(100, {{1, 50}});
    EXPECT_TRUE(r.gameOver);
    EXPECT_TRUE(g.over());
    EXPECT_EQ(r.round, 2);
}

TEST(resurrection_resets_streak, "воскрешение сбрасывает счёт ходов «все мертвы»") {
    Game g;
    g.addKid(1);
    kill(g, 1);
    g.playRound(40, {{1, 40}});
    EXPECT_FALSE(g.over());
    kill(g, 1);
    EXPECT_FALSE(g.over());
    g.playRound(100, {{1, 1}});
    EXPECT_TRUE(g.over());
}

TEST(no_kids_never_over, "без козлят игра не заканчивается") {
    Game g;
    EXPECT_EQ(g.kidCount(), 0);
    EXPECT_FALSE(g.over());
}

TEST(unknown_throws_ignored, "броски козлят не из игры не считаются и не меняют n") {
    Game g;
    g.addKid(1);
    const auto r = g.playRound(50, {{1, 50}, {99, 1}});
    EXPECT_EQ(r.kids, 1);
    EXPECT_EQ(r.results.size(), 1u);
    EXPECT_FALSE(g.hasKid(99));
}

TEST(kid_without_throw_keeps_state, "козлёнок, не бросивший в ходу, сохраняет состояние") {
    Game g;
    g.addKid(1);
    g.addKid(2);
    const auto r = g.playRound(100, {{1, 1}});
    EXPECT_EQ(r.kids, 1);
    EXPECT_TRUE(g.isAlive(2));
    EXPECT_EQ(r.dead, 1);
}

TEST(reset_and_clear, "reset: все снова живы, раунды с нуля; clear: никого нет") {
    Game g;
    g.addKid(1);
    kill(g, 1);
    g.reset();
    EXPECT_TRUE(g.isAlive(1));
    EXPECT_EQ(g.round(), 0);
    EXPECT_FALSE(g.over());
    g.clear();
    EXPECT_EQ(g.kidCount(), 0);
}

TEST(rounds_numbered, "раунды нумеруются с 1 подряд") {
    Game g;
    g.addKid(1);
    EXPECT_EQ(g.playRound(50, {{1, 50}}).round, 1);
    EXPECT_EQ(g.playRound(50, {{1, 50}}).round, 2);
    EXPECT_EQ(g.round(), 2);
}
