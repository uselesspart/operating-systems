#include "AppConfig.hpp"

#include <fstream>
#include <string>
#include <string_view>
#include <system_error>

namespace application {
namespace {

std::string_view trim(std::string_view text) {
    constexpr std::string_view whitespace = " \t\r\n";
    const auto first = text.find_first_not_of(whitespace);
    if (first == std::string_view::npos) {
        return {};
    }

    const auto last = text.find_last_not_of(whitespace);
    return text.substr(first, last - first + 1);
}

} // namespace

std::optional<AppConfig> load_config(const Path& config_path) {
    std::error_code error;
    Path base = std::filesystem::current_path(error);
    if (error) {
        return std::nullopt;
    }
    return load_config(config_path, base);
}

std::optional<AppConfig> load_config(const Path& config_path, const Path& target_base) {
    if (config_path.empty()) {
        return std::nullopt;
    }

    std::error_code error;
    const Path absolute_config = std::filesystem::absolute(config_path, error);
    if (error || !std::filesystem::is_regular_file(absolute_config, error) || error) {
        return std::nullopt;
    }

    std::ifstream stream{absolute_config};
    if (!stream) {
        return std::nullopt;
    }

    AppConfig config;
    bool found_files = false;
    for (std::string line; std::getline(stream, line);) {
        const std::string_view content = trim(line);
        if (content.empty()) {
            continue;
        }

        const auto equals = content.find('=');
        if (equals == std::string_view::npos || trim(content.substr(0, equals)) != "files" ||
            found_files) {
            return std::nullopt;
        }
        found_files = true;

        const std::string_view values = content.substr(equals + 1);
        for (std::size_t start = 0; start <= values.size();) {
            const auto comma = values.find(',', start);
            const std::string_view item = trim(values.substr(start, comma - start));
            if (item.empty()) {
                return std::nullopt;
            }

            Path target{std::string{item}};
            if (target.is_relative()) {
                target = target_base / target;
            }
            config.targets.push_back(target.lexically_normal());

            if (comma == std::string_view::npos) {
                break;
            }
            start = comma + 1;
        }
    }

    if (stream.bad() || !found_files) {
        return std::nullopt;
    }

    return config;
}

} // namespace application
