#include "options.hpp"
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>

namespace lab {
namespace {
unsigned long long integer(const std::string& text, unsigned long long maximum) {
    if (text.empty() || text.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Expected an unsigned integer: " + text);
    std::size_t used = 0;
    const auto value = std::stoull(text, &used);
    if (used != text.size() || value > maximum) throw std::invalid_argument("Integer out of range: " + text);
    return value;
}
std::vector<int> sequence(const std::string& text) {
    if (text.empty() || text.back() == ',') throw std::invalid_argument("Empty sequence item");
    std::vector<int> values;
    std::istringstream stream(text);
    std::string part;
    while (std::getline(stream, part, ',')) {
        const int value = static_cast<int>(integer(part, 100));
        if (value == 0) throw std::invalid_argument("Sequence values must be in 1..100");
        values.push_back(value);
    }
    return values;
}
}
Options parseOptions(int argc, char** argv) {
    Options options;
    options.seed = std::random_device{}();
    options.logPath = "lab2_" TYPE_CODE ".log";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--headless") options.headless = true;
        else if (arg == "--exit-on-finish") options.exitOnFinish = true;
        else if (arg == "--help") options.help = true;
        else {
            if (i + 1 == argc) throw std::invalid_argument("Missing value for " + arg);
            const std::string value = argv[++i];
            if (arg == "--seed") options.seed = static_cast<std::uint32_t>(integer(value, std::numeric_limits<std::uint32_t>::max()));
            else if (arg == "--round-limit") {
                options.roundLimit = static_cast<int>(integer(value, 1000000));
                if (options.roundLimit == 0) throw std::invalid_argument("Round limit must be positive");
            }
            else if (arg == "--delay-ms") options.delayMs = static_cast<int>(integer(value, 3000));
            else if (arg == "--log") {
                if (value.empty()) throw std::invalid_argument("Log path is empty");
                options.logPath = value;
            }
            else if (arg == "--wolf-sequence") options.wolves = sequence(value);
            else if (arg == "--goat-sequence") options.goats = sequence(value);
            else throw std::invalid_argument("Unknown option: " + arg);
        }
    }
    return options;
}
std::string usage() {
    return "Variant 1: wolf and one child goat; transport=" TYPE_CODE "\n"
           "Run without arguments for the Qt GUI (3 seconds per turn).\n"
           "--headless                 JSON output without a window\n"
           "--seed N                   reproducible random generator\n"
           "--wolf-sequence 100,100     deterministic wolf rolls, then random\n"
           "--goat-sequence 1,1         deterministic child rolls, then random\n"
           "--round-limit N            stop after N rounds (optional)\n"
           "--delay-ms N               headless delay, 0..3000 ms\n"
           "--log PATH                 append diagnostic log\n"
           "--exit-on-finish            close GUI after game/error\n"
           "--help                     display this help\n";
}
}
