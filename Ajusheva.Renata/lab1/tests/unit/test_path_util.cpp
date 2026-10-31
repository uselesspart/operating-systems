#include "check.hpp"

#include "PathUtil.hpp"

#include <climits>

#include <sys/stat.h>
#include <unistd.h>

using namespace diskmond;

TEST(path_join) {
    CHECK_EQ(path::join("/a", "b"), "/a/b");
    CHECK_EQ(path::join("/", "b"), "/b");
    CHECK_EQ(path::join("/a/", "b"), "/a/b");
    CHECK_EQ(path::join("/a", ""), "/a");
    CHECK_EQ(path::join("", "b"), "b");
}

TEST(path_parent_and_name) {
    CHECK_EQ(path::parentOf("/a/b/c"), "/a/b");
    CHECK_EQ(path::parentOf("/a"), "/");
    CHECK_EQ(path::parentOf("/"), "/");
    CHECK_EQ(path::parentOf("relative"), ".");
    CHECK_EQ(path::nameOf("/a/b/c"), "c");
    CHECK_EQ(path::nameOf("plain"), "plain");
}

TEST(path_is_within) {
    CHECK(path::isWithin("/a/b", "/a/b"));
    CHECK(path::isWithin("/a/b/c/d", "/a/b"));
    CHECK(!path::isWithin("/a/bc", "/a/b"));  
    CHECK(!path::isWithin("/a", "/a/b"));
    CHECK(path::isWithin("/anything", "/"));
}

TEST(path_normalize) {
    CHECK_EQ(path::normalize("/a//b/./c/.."), "/a/b");
    CHECK_EQ(path::normalize("/a/b/"), "/a/b");
    CHECK_EQ(path::normalize("/../.."), "/");
    CHECK_EQ(path::normalize("/"), "/");
    CHECK_EQ(path::normalize("a/../.."), "..");
    CHECK_EQ(path::normalize("./x"), "x");
    CHECK_EQ(path::normalize(""), ".");
}

TEST(path_absolute_uses_working_directory) {
    char cwd[PATH_MAX];
    CHECK(::getcwd(cwd, sizeof cwd) != nullptr);
    CHECK_EQ(path::absolute("x/../y"), path::join(cwd, "y"));
    CHECK_EQ(path::absolute("/already//absolute"), "/already/absolute");
}

TEST(path_canonical_and_resolve) {
    unit::TempDir dir;
    CHECK(::mkdir((dir / "real").c_str(), 0755) == 0);
    CHECK(::symlink((dir / "real").c_str(), (dir / "link").c_str()) == 0);

    CHECK_EQ(path::canonical(dir / "link"), dir / "real");
    CHECK(!path::canonical(dir / "missing"));

    
    CHECK_EQ(path::resolve(dir / "link/not/yet/file.log"), dir / "real/not/yet/file.log");
    CHECK_EQ(path::resolve(dir / "link"), dir / "real");
}

TEST(path_file_types) {
    unit::TempDir dir;
    unit::writeFile(dir / "file", "x");
    CHECK(path::isDirectory(dir.path()));
    CHECK(!path::isDirectory(dir / "file"));
    CHECK(path::isRegularFile(dir / "file"));
    CHECK(!path::isRegularFile(dir.path()));
    CHECK(!path::isRegularFile(dir / "missing"));
}
