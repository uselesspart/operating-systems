#include "check.hpp"

#include "link/Names.hpp"
#include "link/WorkDir.hpp"

#include <fstream>
#include <string>

#include <fcntl.h>
#include <semaphore.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace chatlab;

namespace {

bool exists(const std::string& path) {
    return ::access(path.c_str(), F_OK) == 0;
}

pid_t deadPid() {
    const pid_t child = ::fork();
    if (child == 0) {
        ::_exit(0);
    }
    ::waitpid(child, nullptr, 0);
    return child;
}

}

TEST(workdir_names_depend_on_host_and_slot) {
    const ConnKey key{1234, 5};
    CHECK_EQ(names::workDir(1234), std::string("/tmp/lab2chat-1234"));
    CHECK_EQ(names::fifoPath(key, names::kUp), std::string("/tmp/lab2chat-1234/c5.up.fifo"));
    CHECK_EQ(names::socketPath(key), std::string("/tmp/lab2chat-1234/c5.sock"));
    CHECK_EQ(names::semaphoreName(key, names::kDown), std::string("/lab2chat-1234-c5-down"));
    CHECK(names::semaphoreName(key, names::kDown).rfind(names::semaphorePrefix(1234), 0) == 0);
}

TEST(workdir_is_created_and_removed_with_contents) {
    const pid_t fake = deadPid();
    std::string path;
    {
        WorkDir dir(fake);
        path = dir.path();
        CHECK(exists(path));
        std::ofstream(path + "/junk") << "x";
        CHECK(exists(path + "/junk"));
    }
    CHECK(!exists(path));
}

TEST(workdir_sweeps_leftovers_of_dead_hosts_only) {
    const pid_t dead = deadPid();
    const std::string deadDir = names::workDir(dead);
    ::mkdir(deadDir.c_str(), 0700);
    std::ofstream(deadDir + "/c1.sock") << "x";
    const std::string deadSem = names::semaphoreName({dead, 1}, names::kUp);
    sem_t* handle = ::sem_open(deadSem.c_str(), O_CREAT, 0600, 0);
    CHECK(handle != SEM_FAILED);
    ::sem_close(handle);

    const std::string liveDir = names::workDir(::getpid());
    ::mkdir(liveDir.c_str(), 0700);

    CHECK(WorkDir::sweepLeftovers() >= 2);
    CHECK(!exists(deadDir));
    CHECK(!exists("/dev/shm/sem." + deadSem.substr(1)));
    CHECK(exists(liveDir));
    ::rmdir(liveDir.c_str());
}
