#include "cleaner.hpp"
#include "test_utils.hpp"

#include <gtest/gtest.h>

using namespace dirclean;
using testutil::listNames;
using testutil::TempDir;
using testutil::writeFile;

TEST(Cleaner, RemovesEverythingWhenIgnfileIsAbsent) {
    TempDir tmp;
    writeFile(tmp / "d" / "a.txt");
    writeFile(tmp / "d" / ".hidden");
    writeFile(tmp / "d" / "sub" / "deep" / "b.txt");

    CleanReport rep = applyRule({(tmp / "d").string(), "keep.txt"});

    EXPECT_EQ(rep.status, CleanStatus::Cleaned);
    EXPECT_EQ(rep.removed, 3u);
    EXPECT_EQ(rep.failed, 0u);
    EXPECT_TRUE(std::filesystem::is_directory(tmp / "d"));  // сама папка остаётся
    EXPECT_TRUE(listNames(tmp / "d").empty());
}

TEST(Cleaner, DoesNothingWhenIgnfileIsPresent) {
    TempDir tmp;
    writeFile(tmp / "d" / "keep.txt");
    writeFile(tmp / "d" / "a.txt");
    writeFile(tmp / "d" / "sub" / "b.txt");

    CleanReport rep = applyRule({(tmp / "d").string(), "keep.txt"});

    EXPECT_EQ(rep.status, CleanStatus::Ignored);
    EXPECT_EQ(rep.removed, 0u);
    EXPECT_EQ(listNames(tmp / "d"), (std::vector<std::string>{"a.txt", "keep.txt", "sub"}));
}

TEST(Cleaner, IgnfileCanBeAHiddenFile) {
    TempDir tmp;
    writeFile(tmp / "d" / ".ignore");
    writeFile(tmp / "d" / "a.txt");

    EXPECT_EQ(applyRule({(tmp / "d").string(), ".ignore"}).status, CleanStatus::Ignored);
    EXPECT_EQ(listNames(tmp / "d").size(), 2u);
}

TEST(Cleaner, IgnfileCanBeADirectory) {
    TempDir tmp;
    std::filesystem::create_directories(tmp / "d" / "keep");
    writeFile(tmp / "d" / "a.txt");

    EXPECT_EQ(applyRule({(tmp / "d").string(), "keep"}).status, CleanStatus::Ignored);
    EXPECT_EQ(listNames(tmp / "d").size(), 2u);
}

TEST(Cleaner, IgnfileWithAbsolutePathIsHonoured) {
    TempDir tmp;
    writeFile(tmp / "d" / "a.txt");
    writeFile(tmp / "marker");

    CleanReport rep = applyRule({(tmp / "d").string(), (tmp / "marker").string()});

    EXPECT_EQ(rep.status, CleanStatus::Ignored);
    EXPECT_EQ(listNames(tmp / "d").size(), 1u);
}

TEST(Cleaner, IgnfileInSubfolderDoesNotProtect) {
    TempDir tmp;
    writeFile(tmp / "d" / "sub" / "keep.txt");

    EXPECT_EQ(applyRule({(tmp / "d").string(), "keep.txt"}).status, CleanStatus::Cleaned);
    EXPECT_TRUE(listNames(tmp / "d").empty());
}

TEST(Cleaner, EmptyFolderIsCleanedWithoutRemovals) {
    TempDir tmp;
    std::filesystem::create_directories(tmp / "d");

    CleanReport rep = applyRule({(tmp / "d").string(), "keep.txt"});

    EXPECT_EQ(rep.status, CleanStatus::Cleaned);
    EXPECT_EQ(rep.removed, 0u);
}

TEST(Cleaner, MissingFolderIsReported) {
    TempDir tmp;
    EXPECT_EQ(applyRule({(tmp / "nope").string(), "keep.txt"}).status, CleanStatus::NotDirectory);
}

TEST(Cleaner, RegularFileInsteadOfFolderIsReported) {
    TempDir tmp;
    writeFile(tmp / "file.txt", "data");

    EXPECT_EQ(applyRule({(tmp / "file.txt").string(), "keep.txt"}).status,
              CleanStatus::NotDirectory);
    EXPECT_TRUE(std::filesystem::exists(tmp / "file.txt"));
}

TEST(Cleaner, RefusesToCleanRootDirectory) {
    EXPECT_EQ(applyRule({"/", "keep.txt"}).status, CleanStatus::Refused);
    EXPECT_EQ(applyRule({"/tmp/..", "keep.txt"}).status, CleanStatus::Refused);
}

TEST(Cleaner, SymlinkIsRemovedButTargetSurvives) {
    TempDir tmp;
    writeFile(tmp / "outside" / "precious.txt", "data");
    writeFile(tmp / "d" / "a.txt");
    std::filesystem::create_directory_symlink(tmp / "outside", tmp / "d" / "link");

    CleanReport rep = applyRule({(tmp / "d").string(), "keep.txt"});

    EXPECT_EQ(rep.status, CleanStatus::Cleaned);
    EXPECT_TRUE(listNames(tmp / "d").empty());
    EXPECT_TRUE(std::filesystem::exists(tmp / "outside" / "precious.txt"));
}

TEST(Cleaner, SymlinkedFolderIsFollowed) {
    TempDir tmp;
    writeFile(tmp / "real" / "a.txt");
    std::filesystem::create_directory_symlink(tmp / "real", tmp / "alias");

    EXPECT_EQ(applyRule({(tmp / "alias").string(), "keep.txt"}).status, CleanStatus::Cleaned);
    EXPECT_TRUE(listNames(tmp / "real").empty());
}
