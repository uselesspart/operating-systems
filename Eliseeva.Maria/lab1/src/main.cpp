// Демон dirclean (вариант 14): периодически удаляет содержимое папок из конфига,
// если в них нет файла-"защиты". 

#include "daemon.hpp"

int main(int argc, char *argv[]) {
    const char *config = (argc > 1) ? argv[1] : "daemon.conf";
    return dirclean::Daemon::instance().run(config);
}
