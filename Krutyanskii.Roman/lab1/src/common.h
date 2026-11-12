#ifndef LAB1_COMMON_H
#define LAB1_COMMON_H

#include <cstdio>
#include <string>

namespace lab1 {

/// Default interval between two passes over the configured folders, in seconds.
/// Variant 14 of the statement fixes it to 20 seconds; a rule may override it
/// with the optional third field of its configuration line.
constexpr int kScanIntervalSeconds = 20;

/// Upper bound accepted for a configured interval, so that a typo cannot leave
/// the daemon asleep for years.
constexpr long kMaxIntervalSeconds = 86400;

/// Name reported to the system journal.
constexpr const char* kLogIdent = "lab1daemon";

/// File the daemon uses to publish its pid and to find a previous instance.
constexpr const char* kPidFilePath = "/tmp/lab1daemon.pid";

/// Config file used when the daemon is started without an explicit argument.
constexpr const char* kDefaultConfigFileName = "lab1.conf";

/// Ident returned by Daemon::run().
enum class RunResult {
    kSuccess,
    kInvalidArguments,
    kConfigNotFound,
    kConfigInvalid,
    kPreviousInstanceRunning,
    kDaemonizationFailed,
    kPidFileFailed,
};

/// Returns a human readable description of the result.
const char* describe(RunResult result) noexcept;

/// Prints usage information to the given stream (stdout for an explicit
/// --help, stderr when the command line is wrong).
void printUsage(const char* programName, std::FILE* stream) noexcept;

/// Joins two path components with a single '/' separator.
std::string joinPath(const std::string& directory, const std::string& name);

}  // namespace lab1

#endif  // LAB1_COMMON_H
