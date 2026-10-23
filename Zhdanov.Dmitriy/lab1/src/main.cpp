#include "Daemon.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>

int main(int argc, char* argv[]) {
    if (argc > 2) {
        std::cerr << "Использование: " << argv[0] << " [файл-событий]" << std::endl;
        return 1;
    }

    std::string config = "events.conf";
    if (argc == 2) {
        config = argv[1];
    } else {
        const char* fromEnvironment = std::getenv("REMINDER_CONFIG");
        if (fromEnvironment != nullptr && *fromEnvironment != '\0') {
            config = fromEnvironment;
        }
    }

    std::string configPath = config;
    if (configPath.empty() || configPath.front() != '/') {
        char directory[4096];
        if (getcwd(directory, sizeof(directory)) == nullptr) {
            std::cerr << "reminder_daemon: не удалось определить текущий каталог: "
                      << std::strerror(errno) << std::endl;
            return 1;
        }
        configPath = std::string(directory) + "/" + configPath;
    }

    std::ofstream probe(configPath, std::ios::app);
    if (!probe) {
        std::cerr << "reminder_daemon: не удалось открыть " << configPath << std::endl;
        return 1;
    }
    probe.close();

    return Daemon::instance().run(configPath);
}
