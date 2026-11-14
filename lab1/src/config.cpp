#include "config.hpp"

#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

namespace {

std::string trim(const std::string& s) {
    const auto b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return "";
    const auto e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

std::string resolve(const std::string& value, const fs::path& base) {
    fs::path p(value);
    if (p.is_relative()) p = base / p;
    return p.lexically_normal().string();
}

bool parseInterval(const std::string& v, unsigned& out) {
    if (v.empty() || v.size() > 9) return false;
    for (char c : v)
        if (c < '0' || c > '9') return false;
    out = static_cast<unsigned>(std::stoul(v));
    return out > 0;
}

}  // namespace

std::string absolutePath(const std::string& path) {
    std::error_code ec;
    fs::path a = fs::absolute(path, ec);
    if (ec) return path;
    return a.lexically_normal().string();
}

bool parseConfig(const std::string& path, Config& out, std::string& error) {
    std::ifstream in(path);
    if (!in) {
        error = "cannot open config file: " + path;
        return false;
    }
    const fs::path base = fs::path(absolutePath(path)).parent_path();

    Config cfg;
    bool hasInterval = false;
    std::string line;
    int lineNo = 0;
    while (std::getline(in, line)) {
        ++lineNo;
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        line = trim(line);
        if (line.empty()) continue;

        const auto sp = line.find_first_of(" \t");
        const std::string key = line.substr(0, sp);
        const std::string value = (sp == std::string::npos) ? "" : trim(line.substr(sp));
        const std::string where = "line " + std::to_string(lineNo) + ": ";

        if (value.empty()) {
            error = where + "missing value for '" + key + "'";
            return false;
        }
        if (key == "interval") {
            if (!parseInterval(value, cfg.interval)) {
                error = where + "interval must be a positive integer";
                return false;
            }
            hasInterval = true;
        } else if (key == "dir1") {
            cfg.dir1 = resolve(value, base);
        } else if (key == "dir2") {
            cfg.dir2 = resolve(value, base);
        } else {
            error = where + "unknown key '" + key + "'";
            return false;
        }
    }
    if (!hasInterval) {
        error = "'interval' is not set";
        return false;
    }
    if (cfg.dir1.empty()) {
        error = "'dir1' is not set";
        return false;
    }
    out = cfg;
    return true;
}
