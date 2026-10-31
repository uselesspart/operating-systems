#include "check.hpp"

#include "PidFile.hpp"

#include <filesystem>

#include <sys/stat.h>
#include <unistd.h>

using namespace diskmond;

namespace {

std::size_t entriesIn(const std::string& directory) {
    std::size_t count = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(directory)) {
        ++count;
    }
    return count;
}

} 

TEST(pidfile_write_and_read) {
    unit::TempDir dir;
    const PidFile file(dir / "d.pid");
    std::string error;

    CHECK(!file.load());  
    CHECK(file.store(4242, error));
    CHECK_EQ(file.load(), 4242);
    CHECK_EQ(unit::readFile(dir / "d.pid"), "4242\n");
    CHECK_EQ(entriesIn(dir.path()), 1u);  

    CHECK(file.store(7, error));  
    CHECK_EQ(file.load(), 7);
}

TEST(pidfile_rejects_garbage) {
    unit::TempDir dir;
    const PidFile file(dir / "d.pid");
    for (const char* content : {"", "\n", "abc\n", "-5\n", "0\n", "12x\n", "99999999999\n", "1 2\n"}) {
        unit::writeFile(dir / "d.pid", content);
        CHECK(!file.load());
    }
    unit::writeFile(dir / "d.pid", "  77 \r\n");
    CHECK_EQ(file.load(), 77);
}

TEST(pidfile_removed_only_by_owner) {
    unit::TempDir dir;
    const PidFile file(dir / "d.pid");
    std::string error;
    CHECK(file.store(100, error));

    file.release(200);  
    CHECK_EQ(file.load(), 100);

    file.release(100);
    CHECK(::access((dir / "d.pid").c_str(), F_OK) != 0);
}

TEST(pidfile_reports_write_errors) {
    unit::TempDir dir;
    const PidFile file(dir / "no/such/dir/d.pid");
    std::string error;
    CHECK(!file.store(1, error));
    CHECK(unit::contains(error, "No such file or directory"));
}

TEST(pidfile_does_not_write_through_symlink) {
    
    unit::TempDir dir;
    unit::writeFile(dir / "victim", "secret\n");
    CHECK(::symlink((dir / "victim").c_str(), (dir / "d.pid").c_str()) == 0);

    std::string error;
    CHECK(PidFile(dir / "d.pid").store(1234, error));
    CHECK_EQ(unit::readFile(dir / "victim"), "secret\n");
    struct stat info {};
    CHECK(::lstat((dir / "d.pid").c_str(), &info) == 0 && S_ISREG(info.st_mode));
    CHECK_EQ(unit::readFile(dir / "d.pid"), "1234\n");
}

TEST(pidfile_default_location) {
    const std::string expected = ::geteuid() == 0 ? std::string("/run/diskmond.pid")
                                                  : "/tmp/diskmond-" + std::to_string(::geteuid()) + ".pid";
    CHECK_EQ(defaultPidFileLocation(), expected);
}
