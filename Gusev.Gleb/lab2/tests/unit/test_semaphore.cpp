#include <sys/wait.h>
#include <unistd.h>

#include <chrono>

#include "ipc/LocalSemaphore.h"
#include "test.h"

TEST_SUITE("LocalSemaphore — sem_init в общей памяти, виден потомку");

using namespace std::chrono;
using namespace std::chrono_literals;

namespace {

struct Shared {
    sem_t* base;
    int count;
    explicit Shared(int n) : base(wolf::sem::createShared(n)), count(n) {}
    ~Shared() { wolf::sem::destroyShared(base, count); }
};

}

TEST(initial_zero, "новый семафор равен нулю: wait сразу истекает") {
    Shared shared(1);
    EXPECT_FALSE(wolf::sem::wait(shared.base, 50ms));
}

TEST(post_then_wait, "post, затем wait — сразу успешно") {
    Shared shared(1);
    EXPECT_TRUE(wolf::sem::post(shared.base));
    EXPECT_TRUE(wolf::sem::wait(shared.base, 100ms));
}

TEST(counts_posts, "два post — два успешных wait, третий истекает") {
    Shared shared(1);
    wolf::sem::post(shared.base);
    wolf::sem::post(shared.base);
    EXPECT_TRUE(wolf::sem::wait(shared.base, 50ms));
    EXPECT_TRUE(wolf::sem::wait(shared.base, 50ms));
    EXPECT_FALSE(wolf::sem::wait(shared.base, 50ms));
}

TEST(timeout, "wait без post возвращается по тайм-ауту") {
    Shared shared(1);
    const auto start = steady_clock::now();
    EXPECT_FALSE(wolf::sem::wait(shared.base, 300ms));
    const auto ms = duration_cast<milliseconds>(steady_clock::now() - start).count();
    CONTEXT("ждали " << ms << " мс");
    EXPECT_TRUE(ms >= 290 && ms < 1000);
}

TEST(fork_visibility, "потомок делает post — родитель просыпается") {
    Shared shared(1);
    const pid_t child = ::fork();
    if (child == 0) {
        ::usleep(100 * 1000);
        wolf::sem::post(shared.base);
        ::_exit(0);
    }
    EXPECT_TRUE(wolf::sem::wait(shared.base, 2s));
    int status = 0;
    ::waitpid(child, &status, 0);
    EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

TEST(two_directions, "два семафора: родитель и потомок будят друг друга") {
    Shared shared(2);
    const pid_t child = ::fork();
    if (child == 0) {
        wolf::sem::post(&shared.base[0]);
        const bool ok = wolf::sem::wait(&shared.base[1], 2s);
        ::_exit(ok ? 0 : 1);
    }
    EXPECT_TRUE(wolf::sem::wait(&shared.base[0], 2s));
    EXPECT_TRUE(wolf::sem::post(&shared.base[1]));
    int status = 0;
    ::waitpid(child, &status, 0);
    EXPECT_TRUE(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}
