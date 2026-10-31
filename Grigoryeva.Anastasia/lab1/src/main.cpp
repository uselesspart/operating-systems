#include <cstring>
#include <iostream>

#include "Daemon.h"

int main(int argc, char* argv[]) {
    std::string config = "reminder.conf";  // по умолчанию - файл в рабочей папке запуска
    if (argc > 2 || (argc == 2 && (std::strcmp(argv[1], "-h") == 0 || std::strcmp(argv[1], "--help") == 0))) {
        std::cout << "Использование: " << argv[0] << " [путь_к_конфигу]\n"
                  << "По умолчанию читается ./reminder.conf\n";
        return argc > 2 ? 1 : 0;
    }
    if (argc == 2) config = argv[1];
    return Daemon::instance().run(config);
}
