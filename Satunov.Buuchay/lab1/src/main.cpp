#include "daemon.hpp"

#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config-file>\n";
        return 1;
    }

    try {
        const std::filesystem::path config_path =
            std::filesystem::absolute(argv[1]);

        return Daemon::run(config_path);
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}