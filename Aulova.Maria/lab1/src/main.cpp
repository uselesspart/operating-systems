
#include "Daemon.h"

#include <exception>
#include <iostream>
#include <string>
#include <vector>

int main(int argc, char* argv[]) {
    const std::string program = argc > 0 ? argv[0] : "daemon_lab1";
    std::vector<std::string> args;
    for (int i = 1; i < argc; ++i) {
        args.emplace_back(argv[i]);
    }

    Daemon::Options options;
    std::string error;
    if (!Daemon::parseArguments(args, options, error)) {
        std::cerr << "daemon_lab1: " << error << "\n\n" << Daemon::usage(program);
        return 2;
    }
    if (options.help) {
        std::cout << Daemon::usage(program);
        return 0;
    }

    try {
        return Daemon::instance().run(options);
    } catch (const std::exception& e) {
        std::cerr << "daemon_lab1: fatal error: " << e.what() << '\n';
        return 1;
    }
}
