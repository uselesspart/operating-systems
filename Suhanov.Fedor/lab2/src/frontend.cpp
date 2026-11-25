#include "frontend.hpp"
#include "ipc.hpp"
#include "protocol.hpp"
#include <charconv>
#include <iostream>

namespace application {
namespace {
int parse_pid(const std::string& value) {
    int pid = 0;
    const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), pid);
    if (error != std::errc{} || end != value.data() + value.size() || pid <= 1) {
        throw Error("PID must be a positive integer greater than 1");
    }
    return pid;
}
} // namespace
Options parse_options(int argc, char** argv, bool host) {
    Options options;
    options.name = host ? "host" : "";
    bool has_host_pid = false;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--name" || argument == "--host") {
            if (++i >= argc) { throw Error("Missing value for " + argument); }
            if (argument == "--name") { options.name = argv[i]; }
            else {
                if (host) { throw Error("--host is only valid for clients"); }
                options.host_pid = parse_pid(argv[i]);
                has_host_pid = true;
            }
        } else if (!host && !has_host_pid && !argument.starts_with('-')) {
            options.host_pid = parse_pid(argument);
            has_host_pid = true;
        } else { throw Error("Unknown argument: " + argument); }
    }
    validate_name(options.name);
    if (!host && !options.host_pid) { throw Error("Client requires --host PID and --name NAME"); }
    return options;
}
int entry(int argc, char** argv, bool host) {
    try {
        prepare_signals();
        const auto options = parse_options(argc, argv, host);
        return run_gui(argc, argv, options, host);
    } catch (const std::exception& error) {
        std::cerr << "[error] " << error.what() << std::endl;
        return 1;
    }
}
} // namespace application
