#pragma once
#include <string>

namespace application {
struct Options {
    int host_pid = 0;
    std::string name;
};
Options parse_options(int argc, char** argv, bool host);
int run_gui(int argc, char** argv, const Options& options, bool host);
int entry(int argc, char** argv, bool host);
} // namespace application
