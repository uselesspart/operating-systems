#include "cleaner.hpp"
#include "config.hpp"

#include <chrono>
#include <exception>
#include <iostream>
#include <thread>

namespace {

    constexpr int kIntervalSeconds = 20;

    void process_config(const Config& config) {
        for (const ConfigEntry& entry : config.entries()) {
            Cleaner::clean(entry.folder, entry.depth);
        }
    }

} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <config-file>\n";
        return 1;
    }

    try {
        const Config config = Config::load(argv[1]);

        while (true) {
            process_config(config);
            std::this_thread::sleep_for(
                std::chrono::seconds(kIntervalSeconds)
            );
        }
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}