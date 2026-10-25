#include "config.hpp"

#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>

namespace fs = std::filesystem;

namespace {

int parse_depth(const std::string& value, std::size_t line_number) {
    std::size_t parsed = 0;
    int depth = 0;

    try {
        depth = std::stoi(value, &parsed);
    } catch (const std::exception&) {
        throw std::runtime_error(
            "Invalid depth at line " + std::to_string(line_number)
        );
    }

    if (parsed != value.size() || depth <= 0) {
        throw std::runtime_error(
            "Depth must be a positive integer at line " +
            std::to_string(line_number)
        );
    }

    return depth;
}

} // namespace

Config Config::load(const fs::path& config_path) {
    std::ifstream input(config_path);

    if (!input) {
        throw std::runtime_error(
            "Cannot open configuration file: " + config_path.string()
        );
    }

    Config config;
    const fs::path config_directory =
        fs::absolute(config_path).parent_path();

    std::string line;
    std::size_t line_number = 0;

    while (std::getline(input, line)) {
        ++line_number;

        std::istringstream stream(line);
        std::string folder;
        std::string depth_string;
        std::string extra;

        if (!(stream >> folder)) {
            continue;
        }

        if (folder.starts_with('#')) {
            continue;
        }

        if (!(stream >> depth_string) || stream >> extra) {
            throw std::runtime_error(
                "Expected: folder depth at line " +
                std::to_string(line_number)
            );
        }

        fs::path folder_path(folder);

        if (folder_path.is_relative()) {
            folder_path = config_directory / folder_path;
        }

        config.entries_.push_back({
            fs::weakly_canonical(folder_path),
            parse_depth(depth_string, line_number)
        });
    }

    return config;
}

const std::vector<ConfigEntry>& Config::entries() const noexcept {
    return entries_;
}