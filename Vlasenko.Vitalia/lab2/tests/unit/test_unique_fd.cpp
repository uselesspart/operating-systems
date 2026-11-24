#include "common/unique_fd.hpp"

#include <cerrno>
#include <fcntl.h>
#include <gtest/gtest.h>
#include <type_traits>
#include <unistd.h>
#include <utility>

namespace lab2::common {
namespace {

TEST(UniqueFdTest, DestructorClosesDescriptor) {
    const int raw = open("/dev/null", O_RDONLY | O_CLOEXEC);
    ASSERT_GE(raw, 0);
    {
        UniqueFd fd(raw);
        EXPECT_TRUE(fd);
    }
    EXPECT_EQ(fcntl(raw, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(UniqueFdTest, MoveAssignmentClosesPreviousDescriptor) {
    UniqueFd first(open("/dev/null", O_RDONLY | O_CLOEXEC));
    UniqueFd second(open("/dev/null", O_RDONLY | O_CLOEXEC));
    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    const int old = second.get();
    const int raw = first.get();
    second = std::move(first);
    EXPECT_FALSE(first);
    EXPECT_EQ(second.get(), raw);
    EXPECT_EQ(fcntl(old, F_GETFD), -1);
    EXPECT_EQ(errno, EBADF);
}

TEST(UniqueFdTest, ReleaseDoesNotCloseDescriptor) {
    UniqueFd fd(open("/dev/null", O_RDONLY | O_CLOEXEC));
    ASSERT_TRUE(fd);
    UniqueFd released(fd.release());
    EXPECT_FALSE(fd);
    EXPECT_TRUE(released);
    EXPECT_GE(fcntl(released.get(), F_GETFD), 0);
}

} // namespace
} // namespace lab2::common
