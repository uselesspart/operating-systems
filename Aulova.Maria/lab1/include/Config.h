#pragma once

#include <filesystem>
#include <string>
#include <vector>

class Config {
public:
    static constexpr int kDefaultInterval = 20;
    static constexpr int kMinInterval = 1;
    static constexpr int kMaxInterval = 86400;

    struct Rule {
        std::filesystem::path folder;
        std::string ignfile;
    };

    static bool load(const std::filesystem::path& file, Config& result,
                     std::vector<std::string>& errors);

    static bool parse(const std::string& text, const std::filesystem::path& baseDir,
                      Config& result, std::vector<std::string>& errors);

    int interval() const { return interval_; }
    const std::vector<Rule>& rules() const { return rules_; }

private:
    int interval_ = kDefaultInterval;
    std::vector<Rule> rules_;
};