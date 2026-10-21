#include <gtest/gtest.h>

#include <sys/stat.h>

#include "cleaner.hpp"
#include "test_utils.hpp"

namespace fs = std::filesystem;
using dirclean::cleanFolderContents;
using dirclean::hasIgnFile;
using testutil::TempDir;
using testutil::writeFile;

TEST(Cleaner, HasIgnFileDetectsGuard) {
    TempDir tmp;
    writeFile(tmp.path() / "keep.txt", "x");

    EXPECT_TRUE(hasIgnFile(tmp.path() / "keep.txt"));
    EXPECT_FALSE(hasIgnFile(tmp.path() / "absent.txt"));
}

TEST(Cleaner, HasIgnFileWorksForDirectories) {
    TempDir tmp;
    fs::create_directories(tmp.path() / "adir");

    EXPECT_TRUE(hasIgnFile(tmp.path() / "adir"));
}

TEST(Cleaner, RemovesFilesSubdirsAndHiddenFiles) {
    TempDir tmp;
    const fs::path dir = tmp.path() / "target";
    writeFile(dir / "a.txt", "a");
    writeFile(dir / ".hidden", "h");
    writeFile(dir / "sub" / "b.txt", "b");
    writeFile(dir / "sub" / "deeper" / "c.txt", "c");

    const std::size_t removed = cleanFolderContents(dir);

    EXPECT_GT(removed, 0u);
    EXPECT_TRUE(fs::is_directory(dir));
    EXPECT_TRUE(testutil::listNames(dir).empty());
}

TEST(Cleaner, EmptyFolderGivesZero) {
    TempDir tmp;
    const fs::path dir = tmp.path() / "empty";
    fs::create_directories(dir);

    EXPECT_EQ(cleanFolderContents(dir), 0u);
    EXPECT_TRUE(fs::is_directory(dir));
}

TEST(Cleaner, RemovesSymlinkWithoutFollowingIt) {
    TempDir tmp;
    const fs::path dir = tmp.path() / "target";
    fs::create_directories(dir);
    const fs::path outside = tmp.path() / "outside.txt";
    writeFile(outside, "important");
    fs::create_symlink(outside, dir / "link.txt");

    cleanFolderContents(dir);

    EXPECT_TRUE(testutil::listNames(dir).empty());
    EXPECT_TRUE(fs::exists(outside));
}

TEST(Cleaner, KeepsFolderItselfAndItsInode) {
    TempDir tmp;
    const fs::path dir = tmp.path() / "target";
    writeFile(dir / "a.txt", "a");
    struct stat before{};
    ASSERT_EQ(::stat(dir.c_str(), &before), 0);

    cleanFolderContents(dir);

    struct stat after{};
    ASSERT_EQ(::stat(dir.c_str(), &after), 0);
    EXPECT_EQ(after.st_ino, before.st_ino);
}
