#include "treecleaner/cleaner.hpp"
#include "treecleaner/config.hpp"
#include "treecleaner/daemon.hpp"
#include "treecleaner/logger.hpp"

#include <chrono>
#include <cstdlib>
#include <csignal>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <unistd.h>

namespace fs = std::filesystem;

namespace {

volatile std::sig_atomic_t g_stopRequested = 0;
volatile std::sig_atomic_t g_reloadRequested = 0;

enum class Mode { Unspecified, Once, Foreground, Daemon };

struct Options {
    Mode mode = Mode::Unspecified;
    fs::path configPath;
    fs::path logPath;
    fs::path pidPath;
    unsigned int intervalSeconds = 60;
    bool logPathExplicit = false;
    bool pidPathExplicit = false;
};

void signalHandler(int signalNumber) {
    if (signalNumber == SIGTERM || signalNumber == SIGINT) {
        g_stopRequested = 1;
    } else if (signalNumber == SIGHUP) {
        g_reloadRequested = 1;
    }
}

void installSignalHandlers() {
    struct sigaction action {};
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;
    if (::sigaction(SIGTERM, &action, nullptr) != 0 ||
        ::sigaction(SIGINT, &action, nullptr) != 0 ||
        ::sigaction(SIGHUP, &action, nullptr) != 0) {
        throw std::runtime_error("не удалось установить обработчики сигналов");
    }
}

fs::path makeAbsolute(const fs::path& path) {
    std::error_code ec;
    fs::path absolutePath = fs::absolute(path, ec);
    if (ec) {
        throw std::runtime_error("не удалось получить абсолютный путь для '" + path.string() + "': " + ec.message());
    }
    return absolutePath.lexically_normal();
}

unsigned int parsePositiveUnsigned(const std::string& value, const std::string& optionName) {
    std::size_t parsed = 0;
    unsigned long number = 0;
    try {
        number = std::stoul(value, &parsed, 10);
    } catch (const std::exception&) {
        throw std::runtime_error("неверное значение для " + optionName + ": " + value);
    }
    if (parsed != value.size() || number == 0 || number > 86400UL ||
        number > static_cast<unsigned long>(std::numeric_limits<unsigned int>::max())) {
        throw std::runtime_error("значение " + optionName + " должно быть в диапазоне 1..86400 секунд");
    }
    return static_cast<unsigned int>(number);
}

void printUsage(std::ostream& output) {
    output <<
        "TreeCleaner - демон удаления подкаталогов на заданной глубине (Linux, C++17)\n\n"
        "Использование:\n"
        "  treecleaner --config FILE --once [--log-file FILE]\n"
        "  treecleaner --config FILE --foreground [--interval SEC] [--log-file FILE]\n"
        "  treecleaner --config FILE --daemon [--interval SEC] [--log-file FILE] [--pid-file FILE]\n\n"
        "Режимы:\n"
        "  --once        выполнить один проход и завершиться (удобно для тестирования)\n"
        "  --foreground  работать в текущем терминале, периодически выполняя проходы\n"
        "  --daemon      запуститься в фоновом режиме\n\n"
        "Параметры:\n"
        "  --config FILE путь к конфигурации; относительные пути в ней считаются от её каталога\n"
        "  --interval N  интервал между проходами в секундах (1..86400, по умолчанию 60)\n"
        "  --log-file F  путь к журналу; без параметра режимы once/foreground пишут в stderr\n"
        "  --pid-file F  путь к PID-файлу (только для --daemon)\n"
        "  --help        показать эту справку\n\n"
        "Формат конфигурации: одна строка 'folder depth'; пустые строки и комментарии # игнорируются.\n"
        "Удаляются каталоги ровно на указанной глубине; сам folder сохраняется.\n";
}

Options parseArguments(int argc, char* argv[]) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--help" || argument == "-h") {
            printUsage(std::cout);
            std::exit(0);
        } else if (argument == "--once" || argument == "--foreground" || argument == "--daemon") {
            if (options.mode != Mode::Unspecified) {
                throw std::runtime_error("нужно указать только один режим запуска");
            }
            if (argument == "--once") options.mode = Mode::Once;
            if (argument == "--foreground") options.mode = Mode::Foreground;
            if (argument == "--daemon") options.mode = Mode::Daemon;
        } else if (argument == "--config" || argument == "--log-file" || argument == "--pid-file" || argument == "--interval") {
            if (i + 1 >= argc) {
                throw std::runtime_error("после " + argument + " нужно указать значение");
            }
            const std::string value = argv[++i];
            if (argument == "--config") {
                options.configPath = value;
            } else if (argument == "--log-file") {
                options.logPath = value;
                options.logPathExplicit = true;
            } else if (argument == "--pid-file") {
                options.pidPath = value;
                options.pidPathExplicit = true;
            } else {
                options.intervalSeconds = parsePositiveUnsigned(value, argument);
            }
        } else {
            throw std::runtime_error("неизвестный параметр: " + argument);
        }
    }

    if (options.mode == Mode::Unspecified) {
        throw std::runtime_error("укажите один из режимов: --once, --foreground или --daemon");
    }
    if (options.configPath.empty()) {
        throw std::runtime_error("необходимо указать --config FILE");
    }
    if (options.pidPathExplicit && options.mode != Mode::Daemon) {
        throw std::runtime_error("--pid-file допустим только вместе с --daemon");
    }

    options.configPath = makeAbsolute(options.configPath);
    if (options.logPathExplicit) {
        options.logPath = makeAbsolute(options.logPath);
    } else if (options.mode == Mode::Daemon) {
        options.logPath = "/tmp/treecleaner-" + std::to_string(static_cast<unsigned long long>(::getuid())) + ".log";
    }
    if (options.mode == Mode::Daemon) {
        if (options.pidPathExplicit) {
            options.pidPath = makeAbsolute(options.pidPath);
        } else {
            options.pidPath = "/tmp/treecleaner-" + std::to_string(static_cast<unsigned long long>(::getuid())) + ".pid";
        }
    }
    return options;
}

struct PassSummary {
    std::size_t removedCount = 0;
    bool hadErrors = false;
};

PassSummary runPass(const std::vector<treecleaner::ConfigEntry>& entries, const treecleaner::Logger& logger) {
    PassSummary summary;
    if (entries.empty()) {
        logger.warning("конфигурация не содержит заданий; проход ничего не сделал");
        return summary;
    }

    for (const auto& entry : entries) {
        logger.info("обработка каталога '" + entry.folder.string() + "' на глубине " + std::to_string(entry.depth));
        const treecleaner::CleanResult result = treecleaner::cleanAtDepth(entry.folder, entry.depth);
        for (const fs::path& removed : result.removedDirectories) {
            logger.info("удалён каталог: '" + removed.string() + "'");
        }
        for (const std::string& error : result.errors) {
            logger.error(error);
            summary.hadErrors = true;
        }
        summary.removedCount += result.removedDirectories.size();
        logger.info("завершена обработка '" + entry.folder.string() + "'; удалено каталогов: " +
                    std::to_string(result.removedDirectories.size()));
    }
    logger.info("проход завершён; всего удалено каталогов: " + std::to_string(summary.removedCount));
    return summary;
}

void waitUntilNextPass(unsigned int intervalSeconds) {
    const auto ticks = static_cast<unsigned long long>(intervalSeconds) * 10ULL;
    for (unsigned long long tick = 0; tick < ticks && !g_stopRequested && !g_reloadRequested; ++tick) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
}

int runContinuous(std::vector<treecleaner::ConfigEntry> entries,
                  const fs::path& configPath,
                  unsigned int intervalSeconds,
                  const treecleaner::Logger& logger) {
    logger.info("TreeCleaner запущен; интервал: " + std::to_string(intervalSeconds) + " сек.");
    while (!g_stopRequested) {
        if (g_reloadRequested) {
            g_reloadRequested = 0;
            try {
                entries = treecleaner::loadConfig(configPath);
                logger.info("конфигурация успешно перечитана по SIGHUP");
            } catch (const std::exception& exception) {
                logger.error(std::string("не удалось перечитать конфигурацию; используется предыдущая: ") + exception.what());
            }
        }
        (void)runPass(entries, logger);
        waitUntilNextPass(intervalSeconds);
    }
    logger.info("получен сигнал остановки; демон завершает работу");
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    try {
        const Options options = parseArguments(argc, argv);
        treecleaner::Logger logger(options.logPath.string());

        std::vector<treecleaner::ConfigEntry> entries = treecleaner::loadConfig(options.configPath);
        logger.info("конфигурация прочитана: записей " + std::to_string(entries.size()));

        if (options.mode == Mode::Once) {
            const PassSummary summary = runPass(entries, logger);
            return summary.hadErrors ? 1 : 0;
        }

        if (options.mode == Mode::Foreground) {
            installSignalHandlers();
            return runContinuous(std::move(entries), options.configPath, options.intervalSeconds, logger);
        }

        treecleaner::PidFile pidFile(options.pidPath);
        std::string daemonError;
        const treecleaner::DaemonizeResult daemonizeResult = treecleaner::daemonize(daemonError);
        if (daemonizeResult == treecleaner::DaemonizeResult::Parent) {
            return 0;
        }
        if (daemonizeResult == treecleaner::DaemonizeResult::Error) {
            logger.error(daemonError);
            pidFile.removeIfOwned();
            return 1;
        }

        try {
            pidFile.writeCurrentPid();
            installSignalHandlers();
            const int exitCode = runContinuous(std::move(entries), options.configPath, options.intervalSeconds, logger);
            pidFile.removeIfOwned();
            return exitCode;
        } catch (const std::exception& exception) {
            logger.error(std::string("ошибка в процессе демона: ") + exception.what());
            pidFile.removeIfOwned();
            return 1;
        }
    } catch (const std::exception& exception) {
        std::cerr << "Ошибка: " << exception.what() << "\n\n";
        printUsage(std::cerr);
        return 2;
    }
}
