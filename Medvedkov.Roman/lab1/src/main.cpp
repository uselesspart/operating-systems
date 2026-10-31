#include "ConfigLoader.hpp"
#include "Daemon.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {
    try {
        if (argc >= 2 && std::string(argv[1]) == "--check-event") {
            if (argc != 3) throw std::runtime_error("--check-event requires one complete event line");
            reminder::ReminderEvent::parse(argv[2]);
            return 0;
        }
        if (argc >= 2 && std::string(argv[1]) == "--check") {
            if (argc != 3) throw std::runtime_error("--check requires a config path");
            reminder::ConfigLoader::load(argv[2]);
            return 0;
        }
        if (argc > 3) throw std::runtime_error("Usage: reminder [config [pidfile]] | --check config | --check-event line");
        const auto config = std::filesystem::absolute(argc > 1 ? argv[1] : "reminder.conf").lexically_normal().string();
        const auto pidfile = std::filesystem::absolute(argc > 2 ? argv[2] : config + ".pid").lexically_normal().string();
        if (config == pidfile) throw std::runtime_error("Config and pidfile must differ");
        return reminder::Daemon::instance().run(config, pidfile);
    } catch (const std::exception& error) {
        std::cerr << "reminder: " << error.what() << '\n';
        return 1;
    }
}
