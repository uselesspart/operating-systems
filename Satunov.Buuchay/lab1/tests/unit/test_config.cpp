#include "config.hpp"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace fs = std::filesystem;

int main() {
    const fs::path directory =
        fs::temp_directory_path() / "os_lab1_config_test";

    fs::remove_all(directory);
    fs::create_directories(directory / "configs");

    {
        std::ofstream output(directory / "configs" / "config.txt");
        output << "# comment\n";
        output << "../tree 2\n";
        output << "/tmp/absolute-tree 1\n";
        output << "\n";
    }

    const Config config =
        Config::load(directory / "configs" / "config.txt");

    assert(config.entries().size() == 2);
    assert(config.entries()[0].depth == 2);
    assert(config.entries()[1].depth == 1);
    assert(config.entries()[0].folder.is_absolute());
    assert(config.entries()[1].folder.is_absolute());

    {
        std::ofstream output(directory / "configs" / "invalid.txt");
        output << "../tree 0\n";
    }

    bool exception_received = false;

    try {
        Config::load(directory / "configs" / "invalid.txt");
    } catch (const std::runtime_error&) {
        exception_received = true;
    }

    assert(exception_received);

    fs::remove_all(directory);
    return 0;
}