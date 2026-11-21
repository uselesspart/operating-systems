#include "check.hpp"

#include "link/GlobalSemaphore.hpp"

#include <chrono>
#include <string>
#include <system_error>

#include <sys/wait.h>
#include <unistd.h>

using namespace chatlab;
using Steady = std::chrono::steady_clock;

namespace {

std::string uniqueName(const char* tag) {
    return "/lab2unit-" + std::to_string(::getpid()) + "-" + tag;
}

bool semaphoreExists(const std::string& name) {
    return ::access(("/dev/shm/sem." + name.substr(1)).c_str(), F_OK) == 0;
}

}

TEST(semaphore_post_then_wait) {
    GlobalSemaphore owner = GlobalSemaphore::create(uniqueName("a"));
    CHECK(owner.post());
    CHECK(owner.waitFor(std::chrono::milliseconds(100)));
}

TEST(semaphore_wait_times_out) {
    GlobalSemaphore owner = GlobalSemaphore::create(uniqueName("b"));
    const auto started = Steady::now();
    CHECK(!owner.waitFor(std::chrono::milliseconds(300)));
    const auto spent = Steady::now() - started;
    CHECK(spent >= std::chrono::milliseconds(280));
    CHECK(spent < std::chrono::milliseconds(1500));
}

TEST(semaphore_attach_sees_posts_of_other_process) {
    const std::string name = uniqueName("c");
    GlobalSemaphore owner = GlobalSemaphore::create(name);
    const pid_t child = ::fork();
    if (child == 0) {
        GlobalSemaphore guest = GlobalSemaphore::attach(name);
        guest.post();
        ::_exit(0);
    }
    CHECK(owner.waitFor(std::chrono::milliseconds(2000)));
    int status = 0;
    ::waitpid(child, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}

TEST(semaphore_owner_unlinks_name) {
    const std::string name = uniqueName("d");
    {
        GlobalSemaphore owner = GlobalSemaphore::create(name);
        CHECK(semaphoreExists(name));
        GlobalSemaphore moved = std::move(owner);
        CHECK(semaphoreExists(name));
    }
    CHECK(!semaphoreExists(name));
    CHECK_THROWS(GlobalSemaphore::attach(name), std::system_error);
}
