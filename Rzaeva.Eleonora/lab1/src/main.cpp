#include <string>

#include "daemon.hpp"

int main(int argc, char** argv) {
    // Путь к конфигу можно задать аргументом, иначе берётся ./daemon.conf.
    const std::string configPath = (argc > 1) ? argv[1] : "daemon.conf";
    return dirclean::Daemon::instance().run(configPath);
}
