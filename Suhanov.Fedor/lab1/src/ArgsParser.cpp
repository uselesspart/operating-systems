#include "ArgParser.hpp"

#include <string>
#include <system_error>
#include <utility>

namespace application {

std::optional<Arguments> parse_args(int argc, const char* argv[]) {
    if (argc != 3) {
        return std::nullopt;
    }
    
    std::string flag{argv[1]};

    if (flag != "--config") {
        return std::nullopt;
    }

    Path config{argv[2]};
    std::error_code error;
    config = std::filesystem::absolute(config, error);

    if (error || !std::filesystem::is_regular_file(config, error) || error) {
        return std::nullopt;
    }

    return Arguments{std::move(config)};
}

} // namespace application
