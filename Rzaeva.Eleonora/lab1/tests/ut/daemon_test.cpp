#include <gtest/gtest.h>

#include <type_traits>

#include "daemon.hpp"

using dirclean::Daemon;

TEST(Daemon, InstanceIsUnique) {
    EXPECT_EQ(&Daemon::instance(), &Daemon::instance());
}

TEST(Daemon, InstanceIsStableAcrossCalls) {
    Daemon& first = Daemon::instance();
    Daemon& second = Daemon::instance();
    EXPECT_EQ(&first, &second);
}

TEST(Daemon, IsNotCopyableOrMovable) {
    static_assert(!std::is_copy_constructible_v<Daemon>);
    static_assert(!std::is_copy_assignable_v<Daemon>);
    static_assert(!std::is_move_constructible_v<Daemon>);
    static_assert(!std::is_move_assignable_v<Daemon>);
    SUCCEED();
}

TEST(Daemon, HasEmptyConfigPathBeforeRun) {
    EXPECT_TRUE(Daemon::instance().configPath().empty());
    EXPECT_TRUE(Daemon::instance().config().rules.empty());
}
