#include "cleaner.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

int main() {
    const fs::path root =
        fs::temp_directory_path() / "os_lab1_cleaner_test";

    fs::remove_all(root);

    fs::create_directories(root / "level1" / "level2" / "level3");

    std::ofstream(root / "root.txt") << "keep";
    std::ofstream(root / "level1" / "file1.txt") << "remove";
    std::ofstream(root / "level1" / "level2" / "file2.txt") << "remove";
    std::ofstream(root / "level1" / "level2" / "level3" / "file3.txt")
        << "keep";

    Cleaner::clean(root, 2);

    assert(fs::exists(root / "root.txt"));
    assert(fs::exists(root / "level1"));
    assert(fs::exists(root / "level1" / "level2"));
    assert(!fs::exists(root / "level1" / "file1.txt"));
    assert(!fs::exists(root / "level1" / "level2" / "file2.txt"));
    assert(fs::exists(root / "level1" / "level2" / "level3" / "file3.txt"));

    fs::remove_all(root);
    return 0;
}