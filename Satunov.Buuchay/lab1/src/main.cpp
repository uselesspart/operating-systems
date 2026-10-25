#include "cleaner.hpp"
#include "config.hpp"

#include <exception>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config-file>\n";
        return 1;
    }

    try {
        const Config config = Config::load(argv[1]);

        for (const ConfigEntry& entry : config.entries()) {
            Cleaner::clean(entry.folder, entry.depth);
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }

    return 0;
}