#include "daemon.hpp"

#include <gtest/gtest.h>
#include <type_traits>

TEST(DaemonTest, InstanceReturnsTheSameObject) {
    EXPECT_EQ(&Daemon::instance(), &Daemon::instance());
}

TEST(DaemonTest, IsNotCopyableNorPublicallyConstructible) {
    static_assert(!std::is_copy_constructible<Daemon>::value, "Daemon must not be copyable");
    static_assert(!std::is_copy_assignable<Daemon>::value, "Daemon must not be assignable");
    static_assert(!std::is_default_constructible<Daemon>::value,
                  "Daemon constructor must be private");
    SUCCEED();
}
