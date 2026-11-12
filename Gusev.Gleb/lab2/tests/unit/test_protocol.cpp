#include <type_traits>

#include "ipc/Protocol.h"
#include "test.h"

TEST_SUITE("Protocol — формат сообщений");

using namespace wolf::proto;

TEST(fixed_size_pod, "сообщение — POD фиксированного размера") {
    EXPECT_TRUE(std::is_trivially_copyable_v<Message>);
    EXPECT_TRUE(std::is_standard_layout_v<Message>);
    EXPECT_EQ(sizeof(Message), 11 * sizeof(std::uint32_t));
}

TEST(builders, "makeRound / makeThrow / makeGameOver / makeBye заполняют поля") {
    const Message r = makeRound(7, false, 3);
    EXPECT_TRUE(r.type == Type::Round);
    EXPECT_EQ(r.round, 7u);
    EXPECT_EQ(r.alive, 0u);
    EXPECT_EQ(r.kids, 3u);

    const Message t = makeThrow(7, 42);
    EXPECT_TRUE(t.type == Type::Throw);
    EXPECT_EQ(t.number, 42);

    EXPECT_TRUE(makeGameOver(9).type == Type::GameOver);
    EXPECT_EQ(makeGameOver(9).round, 9u);
    EXPECT_TRUE(makeBye().type == Type::Bye);
    for (const Message& m : {r, t, makeGameOver(1), makeBye()}) {
        EXPECT_TRUE(isValid(m));
        EXPECT_EQ(m.magic, kMagic);
    }
}

TEST(invalid_rejected, "чужие байты не принимаются за сообщение") {
    Message bad = makeBye();
    bad.magic = 0;
    EXPECT_FALSE(isValid(bad));

    Message badType = makeBye();
    badType.type = static_cast<Type>(77);
    EXPECT_FALSE(isValid(badType));
}

TEST(names, "имена типов и исходов для журнала") {
    EXPECT_EQ(std::string(typeName(Type::Round)), "ROUND");
    EXPECT_EQ(std::string(typeName(Type::GameOver)), "GAME_OVER");
    EXPECT_EQ(std::string(outcomeName(Outcome::Hidden)), "спрятался");
    EXPECT_EQ(std::string(outcomeName(Outcome::Caught)), "попался");
    EXPECT_EQ(std::string(outcomeName(Outcome::Resurrected)), "воскрес");
    EXPECT_EQ(std::string(outcomeName(Outcome::StayedDead)), "остался мёртв");
}
