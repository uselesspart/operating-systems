#include "config.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

namespace dirclean {

Config parseConfig(std::istream &in, const std::string &baseDir,
                   std::vector<std::string> *warnings) {
    Config cfg;
    std::string line;
    int lineNo = 0;

    auto warn = [&](const std::string &msg) {
        if (warnings) warnings->push_back("Config line " + std::to_string(lineNo) + ": " + msg);
    };

    while (std::getline(in, line)) {
        ++lineNo;
        std::istringstream ss(line);
        std::string first, second;
        if (!(ss >> first) || first[0] == '#') continue;

        if (first == "interval") {
            long value = 0;
            if ((ss >> value) && value > 0 && value <= 31536000L) {
                cfg.interval = static_cast<unsigned>(value);
            } else {
                warn("bad interval, ignored");
            }
            continue;
        }

        if (!(ss >> second)) {
            warn("expected '<folder> <ignfile>', ignored");
            continue;
        }

        fs::path folder(first);
        if (folder.is_relative() && !baseDir.empty()) folder = fs::path(baseDir) / folder;
        cfg.rules.push_back({folder.lexically_normal().string(), second});
    }
    return cfg;
}

bool loadConfigFile(const std::string &path, Config &out, std::vector<std::string> *warnings) {
    std::ifstream in(path);
    if (!in) return false;
    out = parseConfig(in, fs::path(path).parent_path().string(), warnings);
    return true;
}

}  