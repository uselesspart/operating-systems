#include "check.hpp"

#include "ProcessInfo.hpp"

#include <chrono>
#include <csignal>
#include <filesystem>
#include <fstream>
#include <thread>

#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

using namespace diskmond;

namespace {


template <typename Condition>
bool eventually(Condition condition) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!condition()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return true;
}

char stateOf(pid_t pid) {
    std::ifstream input("/proc/" + std::to_string(pid) + "/stat");
    std::string line;
    std::getline(input, line);
    const auto commEnd = line.rfind(')');
    return commEnd != std::string::npos && commEnd + 2 < line.size() ? line[commEnd + 2] : '?';
}

pid_t spawn(const std::string& program, const std::string& argument1, const std::string& argument2 = {}) {
    const pid_t child = ::fork();
    if (child == 0) {
        if (argument2.empty()) {
            ::execl(program.c_str(), program.c_str(), argument1.c_str(), static_cast<char*>(nullptr));
        } else {
            ::execl(program.c_str(), program.c_str(), argument1.c_str(), argument2.c_str(),
                    static_cast<char*>(nullptr));
        }
        ::_exit(127);
    }
    return child;
}

void stop(pid_t pid) {
    ::kill(pid, SIGKILL);
    ::waitpid(pid, nullptr, 0);
}

std::string sleepBinary() {
    return std::filesystem::exists("/bin/sleep") ? "/bin/sleep" : "/usr/bin/sleep";
}

}  

TEST(process_current_is_alive) {
    CHECK(process::isAlive(::getpid()));
    CHECK(process::name(::getpid()).has_value());
    CHECK(process::isSameProgram(::getpid()));
}

TEST(process_finished_is_not_alive) {
    const pid_t child = ::fork();
    if (child == 0) {
        ::_exit(0);
    }
    ::waitpid(child, nullptr, 0);
    CHECK(!process::isAlive(child));  
    CHECK(!process::isAlive(0));
    CHECK(!process::isAlive(-1));
}

TEST(process_zombie_is_not_alive) {
    const pid_t child = ::fork();
    if (child == 0) {
        ::_exit(0);
    }
    
    CHECK(eventually([child] { return stateOf(child) == 'Z'; }));
    CHECK(!process::isAlive(child));
    ::waitpid(child, nullptr, 0);
}

TEST(process_other_program_is_recognized) {
    
    unit::TempDir dir;
    const std::string copy = dir / "napper";
    std::filesystem::copy_file(sleepBinary(), copy);
    ::chmod(copy.c_str(), 0755);

    const pid_t child = spawn(copy, "30");
    CHECK(eventually([child] { return process::name(child) == std::string("napper"); }));
    CHECK(process::isAlive(child));
    CHECK(!process::isSameProgram(child));
    ::unlink(copy.c_str());
    CHECK_EQ(process::executableName(child), "napper");
    stop(child);
}

TEST(process_same_name_other_executable) {
    
    const std::string ourName = process::name(::getpid()).value_or("unit");
    const auto ourExecutable = process::executableName(::getpid());
    const pid_t child =
        spawn("/bin/sh", "-c", "printf %s '" + ourName + "' > /proc/$$/comm; while :; do sleep 1; done");
    
    CHECK(eventually(
        [&] { return process::executableName(child) != ourExecutable && process::name(child) == ourName; }));
    CHECK(process::isAlive(child));
    CHECK(!process::isSameProgram(child));
    stop(child);
}
