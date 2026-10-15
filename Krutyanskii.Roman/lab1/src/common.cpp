#include "common.h"

#include <cstdio>

namespace lab1 {

const char* describe(RunResult result) noexcept {
    switch (result) {
        case RunResult::kSuccess:
            return "success";
        case RunResult::kInvalidArguments:
            return "invalid command line arguments";
        case RunResult::kConfigNotFound:
            return "configuration file not found";
        case RunResult::kConfigInvalid:
            return "configuration file is malformed";
        case RunResult::kPreviousInstanceRunning:
            return "another instance refused to stop";
        case RunResult::kDaemonizationFailed:
            return "daemonization failed";
        case RunResult::kPidFileFailed:
            return "unable to write the pid file";
    }
    return "unknown error";
}

void printUsage(const char* programName, std::FILE* stream) noexcept {
    std::fprintf(stream,
                 "Usage: %s [options] [config-file]\n"
                 "\n"
                 "Runs the lab1 daemon in the background. Every %d seconds the daemon\n"
                 "clears the content of every configured folder that does not contain its\n"
                 "guard file.\n"
                 "\n"
                 "Options:\n"
                 "  -h, --help       print this help and exit\n"
                 "      --version    print the version and exit\n"
                 "\n"
                 "Arguments:\n"
                 "  config-file      path to the configuration file, '%s' from the current\n"
                 "                   working directory by default. The absolute path is\n"
                 "                   resolved at startup and remembered by the daemon.\n",
                 programName, kScanIntervalSeconds, kDefaultConfigFileName);
}

std::string joinPath(const std::string& directory, const std::string& name) {
    if (directory.empty()) {
        return name;
    }
    if (directory.back() == '/') {
        return directory + name;
    }
    return directory + '/' + name;
}

}  // namespace lab1
