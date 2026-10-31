#include "config.hpp"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace fs = std::filesystem;

namespace dirclean {
namespace {

std::string trim(const std::string& s) {
    std::size_t begin = 0;
    std::size_t end = s.size();
    while (begin < end && std::isspace(static_cast<unsigned char>(s[begin]))) ++begin;
    while (end > begin && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(begin, end - begin);
}

std::vector<std::string> splitWhitespace(const std::string& s) {
    std::vector<std::string> tokens;
    std::istringstream in(s);
    std::string token;
    while (in >> token) tokens.push_back(token);
    return tokens;
}

fs::path resolveAgainst(const fs::path& baseDir, const std::string& token) {
    fs::path p(token);
    if (p.is_absolute()) return p.lexically_normal();
    return (baseDir / p).lexically_normal();
}

}

ConfigParseResult parseConfigFile(const fs::path& path) {
    ConfigParseResult result;

    std::error_code ec;
    fs::path absPath = fs::absolute(path, ec);
    if (ec) absPath = path;

    std::ifstream file(absPath);
    if (!file.is_open()) {
        result.error = "cannot open config file '" + absPath.string() + "'";
        return result;
    }

    const fs::path baseDir = absPath.parent_path();
    bool sawInterval = false;
    std::string line;
    int lineNumber = 0;

    while (std::getline(file, line)) {
        ++lineNumber;
        const std::size_t comment = line.find('#');
        if (comment != std::string::npos) line.erase(comment);

        const std::string trimmed = trim(line);
        if (trimmed.empty()) continue;

        const std::vector<std::string> tokens = splitWhitespace(trimmed);
        if (tokens[0] == "interval") {
            if (tokens.size() != 2) {
                result.warnings.push_back("line " + std::to_string(lineNumber) +
                                          ": expected 'interval N', line ignored");
                continue;
            }
            try {
                std::size_t consumed = 0;
                const int value = std::stoi(tokens[1], &consumed);
                if (consumed != tokens[1].size() || value <= 0) throw std::invalid_argument("bad");
                result.config.intervalSeconds = value;
                sawInterval = true;
            } catch (const std::exception&) {
                result.warnings.push_back("line " + std::to_string(lineNumber) +
                                          ": interval must be a positive integer, default kept");
            }
            continue;
        }

        if (tokens.size() != 2) {
            result.warnings.push_back("line " + std::to_string(lineNumber) +
                                      ": expected 'folder ignfile', line ignored");
            continue;
        }

        Rule rule;
        rule.folder = resolveAgainst(baseDir, tokens[0]);
        const fs::path ign(tokens[1]);
        rule.ignFile = ign.is_absolute() ? ign.lexically_normal() : (rule.folder / ign).lexically_normal();
        result.config.rules.push_back(std::move(rule));
    }

    if (!sawInterval) {
        result.warnings.push_back("no 'interval' line found, using default " +
                                  std::to_string(result.config.intervalSeconds) + " s");
    }

    result.ok = true;
    return result;
}

}
