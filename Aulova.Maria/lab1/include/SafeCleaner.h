#pragma once

#include "Config.h"

#include <cstddef>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

class SafeCleaner {
public:
    struct Policy {
        std::vector<std::filesystem::path> protectedTrees;
        std::vector<std::filesystem::path> forbiddenExact;
        std::vector<std::filesystem::path> protectedPaths;

        static Policy standard(const std::filesystem::path& configFile,
                               const std::filesystem::path& pidFile);

        static std::vector<std::filesystem::path> windowsDrives(const std::string& mountsText);
    };

    enum class Outcome { Cleaned, Protected, Interrupted, Rejected, Failed };

    using Hook = std::function<void(const std::filesystem::path&)>;

    struct Report {
        std::filesystem::path folder;
        Outcome outcome = Outcome::Rejected;
        std::size_t removedFiles = 0;
        std::size_t removedDirs = 0;
        std::vector<std::string> removedTopLevel;
        std::vector<std::string> messages;
    };

    explicit SafeCleaner(Policy policy);

    void setBeforeRemoveHook(Hook hook);

    bool checkFolder(const std::filesystem::path& folder, std::filesystem::path& resolved,
                     std::string& reason) const;

    std::vector<std::string> validate(const std::vector<Config::Rule>& rules) const;
    std::vector<Report> cleanAll(const std::vector<Config::Rule>& rules) const;
    Report clean(const Config::Rule& rule) const;

private:
    std::vector<std::string> assess(const std::vector<Config::Rule>& rules,
                                    std::vector<std::filesystem::path>& resolved) const;
    Report cleanResolved(const Config::Rule& rule, const std::filesystem::path& resolved) const;

    Policy policy_;
    Hook hook_;
};