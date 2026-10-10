#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "common.h"
#include "daemon.h"
#include "logger.h"

namespace {

bool isOption(const char* argument, const char* shortForm, const char* longForm) {
    return std::strcmp(argument, shortForm) == 0 || std::strcmp(argument, longForm) == 0;
}

bool isLongOption(const char* argument, const char* longForm) {
    return std::strcmp(argument, longForm) == 0;
}

}  // namespace

int main(int argc, char* argv[]) {
    std::string configPath = lab1::kDefaultConfigFileName;

    for (int index = 1; index < argc; ++index) {
        const char* argument = argv[index];

        if (isOption(argument, "-h", "--help")) {
            lab1::printUsage(argv[0], stdout);
            return EXIT_SUCCESS;
        }
        if (isLongOption(argument, "--version")) {
            std::printf("lab1 daemon, variant 14\n");
            return EXIT_SUCCESS;
        }
        if (argument[0] == '-') {
            std::fprintf(stderr, "%s: unknown option '%s'\n", argv[0], argument);
            lab1::printUsage(argv[0], stderr);
            return EXIT_FAILURE;
        }
        if (index + 1 < argc) {
            std::fprintf(stderr, "%s: unexpected argument '%s'\n", argv[0], argv[index + 1]);
            lab1::printUsage(argv[0], stderr);
            return EXIT_FAILURE;
        }
        configPath = argument;
    }

    lab1::log::open(lab1::kLogIdent);
    lab1::log::info("lab1: start requested, config file is looked up in the working directory (%s)",
                    configPath.c_str());

    const lab1::RunResult result = lab1::Daemon::instance().run(configPath);

    if (result != lab1::RunResult::kSuccess) {
        lab1::log::error("lab1: %s", lab1::describe(result));
    }
    lab1::log::info("lab1: terminated with result '%s'", lab1::describe(result));
    lab1::log::close();

    return result == lab1::RunResult::kSuccess ? EXIT_SUCCESS : EXIT_FAILURE;
}
