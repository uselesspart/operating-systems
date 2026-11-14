#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace lab {
struct Options {
    bool headless = false;
    bool exitOnFinish = false;
    bool help = false;
    int roundLimit = 0;
    int delayMs = 0;
    std::uint32_t seed = 0;
    std::string logPath;
    std::vector<int> wolves;
    std::vector<int> goats;
};
Options parseOptions(int argc, char** argv);
std::string usage();
}
