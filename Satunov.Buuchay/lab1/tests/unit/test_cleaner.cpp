
#include "cleaner.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

void test_depth_one(const fs::path& base) {
    const fs::path root = base / "depth_one";

    fs::create_directories(root / "branch" / "child");
    fs::create_directories(root / "empty_branch");

    std::ofstream(root / "root.txt") << "keep";
    std::ofstream(root / "branch" / "child" / "payload.txt") << "remove";

    Cleaner::clean(root, 1);

    assert(fs::exists(root));
    assert(fs::exists(root / "root.txt"));
    assert(!fs::exists(root / "branch"));
    assert(!fs::exists(root / "empty_branch"));
}

void test_depth_two(const fs::path& base) {
    const fs::path root = base / "depth_two";

    fs::create_directories(root / "level1" / "level2" / "level3");

    std::ofstream(root / "root.txt") << "keep";
    std::ofstream(root / "level1" / "file1.txt") << "remove";
    std::ofstream(root / "level1" / "level2" / "file2.txt") << "remove";
    std::ofstream(root / "level1" / "level2" / "level3" / "file3.txt")
        << "remove";

    Cleaner::clean(root, 2);

    assert(fs::exists(root / "root.txt"));
    assert(fs::exists(root / "level1"));
    assert(!fs::exists(root / "level1" / "file1.txt"));
    assert(!fs::exists(root / "level1" / "level2"));
}

void test_depth_three(const fs::path& base) {
    const fs::path root = base / "depth_three";

    fs::create_directories(
        root / "level1" / "level2" / "level3" / "level4"
    );

    std::ofstream(root / "root.txt") << "keep";
    std::ofstream(root / "level1" / "file1.txt") << "remove";
    std::ofstream(root / "level1" / "level2" / "file2.txt") << "remove";
    std::ofstream(root / "level1" / "level2" / "level3" / "file3.txt")
        << "remove";
    std::ofstream(
        root / "level1" / "level2" / "level3" / "level4" / "file4.txt"
    ) << "remove";

    Cleaner::clean(root, 3);

    assert(fs::exists(root / "root.txt"));
    assert(fs::exists(root / "level1"));
    assert(fs::exists(root / "level1" / "level2"));
    assert(!fs::exists(root / "level1" / "file1.txt"));
    assert(!fs::exists(root / "level1" / "level2" / "file2.txt"));
    assert(!fs::exists(root / "level1" / "level2" / "level3"));
}

int main() {
    const fs::path base =
        fs::temp_directory_path() / "os_lab1_cleaner_test";

    fs::remove_all(base);
    fs::create_directories(base);

    test_depth_one(base);
    test_depth_two(base);
    test_depth_three(base);

    fs::remove_all(base);

    return 0;
}
