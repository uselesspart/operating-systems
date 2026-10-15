#ifndef LAB1_CONFIG_H
#define LAB1_CONFIG_H

#include <string>
#include <vector>

#include "common.h"

namespace lab1 {

/// A single "folder ignfile [seconds]" entry taken from the configuration file.
struct Rule {
    std::string folder;
    std::string ignoreFile;
    int intervalSeconds = kScanIntervalSeconds;
};

/// Contents of the configuration file.
///
/// Variant 14 defines the file as an arbitrary number of lines of the form
/// "folder ignfile": the content of "folder" has to be cleared whenever
/// "ignfile" is missing from it. The general part of the statement asks for the
/// interval between two actions to be configured as well, so a line may carry an
/// optional third field with the interval in seconds; without it the rule uses
/// kScanIntervalSeconds.
///
/// Empty lines and lines starting with '#' are ignored. Every other line must
/// hold two or three fields.
class Config {
public:
    /// Parses the file at 'path'. On success the rules are replaced, on failure
    /// the previously loaded rules are kept and false is returned, so that a
    /// broken file cannot break a running daemon.
    bool load(const std::string& path);

    const std::vector<Rule>& rules() const noexcept { return rules_; }

    /// Shortest interval over all rules, used as the tick of the main loop.
    int tickSeconds() const noexcept;

private:
    std::vector<Rule> rules_;
};

}  // namespace lab1

#endif  // LAB1_CONFIG_H
