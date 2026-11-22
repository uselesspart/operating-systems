#include "daemon.h"
#include "events.h"

#include <filesystem>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <syslog.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

int main(int argc, char* argv[]) {
    try {
        const std::vector<std::string> args(argv + 1, argv + argc);
        if ((args.size() == 2 || (args.size() == 4 && args[2] == "--ack-file")) && args[0] == "--show") {
            std::cout << "\nНапоминание\n===========\n\n" << args[1]
                      << "\n\nНажмите Enter, чтобы закрыть окно." << std::endl;
            if (!std::cout) throw std::runtime_error("Не удалось вывести напоминание");
            if (args.size() == 4) {
                const int fd = open(args[3].c_str(), O_WRONLY | O_CLOEXEC | O_NOFOLLOW);
                if (fd == -1) throw std::runtime_error("Не открыть подтверждение: " + std::string(strerror(errno)));
                struct stat info{};
                if (fstat(fd, &info) == -1 || !S_ISREG(info.st_mode) || info.st_uid != getuid() || info.st_nlink != 1) {
                    close(fd);
                    throw std::runtime_error("Некорректный файл подтверждения");
                }
                const ssize_t written = write(fd, "ready\n", 6);
                const int closed = close(fd);
                if (written != 6 || closed == -1) throw std::runtime_error("Не записать подтверждение показа");
            }
            std::string line;
            std::getline(std::cin, line);
            return 0;
        }
        if (!args.empty() && args[0] == "--add") {
            if (args.size() < 5) throw std::runtime_error("add_event YYYY-MM-DD HH:MM[:SS] [-h|-d|-w] text");
            addEvent(std::filesystem::canonical(args[1]).string(), {args.begin() + 2, args.end()});
            std::cout << "Событие добавлено\n";
            return 0;
        }
        if (args.size() == 2 && args[0] == "--check") {
            const auto config = readConfig(std::filesystem::canonical(args[1]).string());
            findTerminal(config.terminal);
            std::cout << "Конфигурация корректна. Событий: " << config.events.size()
                      << ", интервал: " << config.interval << " с\n";
            return 0;
        }
        if (args.empty() || args[0] == "--help") {
            std::cout << "reminder [--foreground] CONFIG [--pid-file PATH]\n"
                         "reminder --check CONFIG\n"
                         "add_event YYYY-MM-DD HH:MM[:SS] [-h|-d|-w] text\n";
            return args.empty() ? 1 : 0;
        }
        std::size_t index = args[0] == "--foreground" ? 1 : 0;
        if (index == args.size()) throw std::runtime_error("Не указан конфигурационный файл");
        const std::string config = std::filesystem::canonical(args[index++]).string();
        std::string pid = config + ".pid";
        if (index < args.size()) {
            if (args.size() != index + 2 || args[index] != "--pid-file")
                throw std::runtime_error("Неизвестные аргументы");
            pid = args[index + 1];
        }
        return Daemon::instance().run(config, pid, args[0] == "--foreground");
    } catch (const std::exception& error) {
        std::cerr << "Ошибка: " << error.what() << '\n';
        openlog("reminder", LOG_PID, LOG_USER);
        syslog(LOG_ERR, "%s", error.what());
        closelog();
        return 1;
    }
}
