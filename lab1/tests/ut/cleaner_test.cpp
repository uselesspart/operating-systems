#include "cleaner.hpp"
#include "test_utils.hpp"

#include <gtest/gtest.h>
#include <unistd.h>

namespace fs = std::filesystem;
using testutil::TempDir;
using testutil::writeFile;

TEST(CleanerTest, RemovesAllSubdirectoriesRecursively) {
    TempDir tmp;
    fs::create_directories(tmp.path + "/a/b/c");
    fs::create_directories(tmp.path + "/d");
    writeFile(tmp.path + "/a/b/c/deep.txt", "x");

    const CleanResult r = removeSubdirectories(tmp.path);
    EXPECT_TRUE(r.ok()) << r.error;
    EXPECT_EQ(2u, r.removed);
    EXPECT_FALSE(fs::exists(tmp.path + "/a"));
    EXPECT_FALSE(fs::exists(tmp.path + "/d"));
}

TEST(CleanerTest, KeepsRegularFilesAndTheFolderItself) {
    TempDir tmp;
    writeFile(tmp.path + "/keep.txt", "x");
    fs::create_directories(tmp.path + "/sub");

    removeSubdirectories(tmp.path);
    EXPECT_TRUE(fs::exists(tmp.path));
    EXPECT_TRUE(fs::exists(tmp.path + "/keep.txt"));
    EXPECT_FALSE(fs::exists(tmp.path + "/sub"));
}

TEST(CleanerTest, EmptyFolderIsFine) {
    TempDir tmp;
    const CleanResult r = removeSubdirectories(tmp.path);
    EXPECT_TRUE(r.ok());
    EXPECT_EQ(0u, r.removed);
}

TEST(CleanerTest, SymlinkToDirectoryIsNotFollowed) {
    TempDir tmp, outside;
    writeFile(outside.path + "/precious.txt", "x");
    ASSERT_EQ(0, symlink(outside.path.c_str(), (tmp.path + "/link").c_str()));

    const CleanResult r = removeSubdirectories(tmp.path);
    EXPECT_TRUE(r.ok()) << r.error;
    EXPECT_EQ(0u, r.removed);
    EXPECT_TRUE(fs::is_symlink(tmp.path + "/link"));
    EXPECT_TRUE(fs::exists(outside.path + "/precious.txt"));
}

TEST(CleanerTest, MissingFolderReportsError) {
    TempDir tmp;
    const CleanResult r = removeSubdirectories(tmp.path + "/does_not_exist");
    EXPECT_FALSE(r.ok());
}

TEST(CleanerTest, FileInsteadOfFolderReportsError) {
    TempDir tmp;
    writeFile(tmp.path + "/f", "x");
    EXPECT_FALSE(removeSubdirectories(tmp.path + "/f").ok());
}
