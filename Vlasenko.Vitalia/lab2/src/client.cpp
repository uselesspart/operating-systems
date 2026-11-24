#include "client/client_controller.hpp"
#include "common/logger.hpp"
#include "handshake/handshake_signals.hpp"
#include "ipc/connection.hpp"

#include <charconv>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <string_view>
#include <system_error>
#include <unistd.h>

namespace {
bool parsePid(std::string_view argument, pid_t& pid) {
    const auto* begin = argument.data();
    const auto* end = begin + argument.size();
    const auto result = std::from_chars(begin, end, pid);
    return result.ec == std::errc{} && result.ptr == end && pid > 0;
}
} // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Usage: " << argv[0] << " <host_pid>\n";
        return EXIT_FAILURE;
    }
    pid_t hostPid{};
    if (!parsePid(argv[1], hostPid)) {
        std::cerr << "Error: invalid host PID\n";
        return EXIT_FAILURE;
    }
    lab2::common::Logger logger;
    try {
        lab2::handshake::blockHandshakeSignals();
        std::cout << "Client started\nIPC transport: " << lab2::ipc::transportName()
                  << "\nClient PID: " << getpid() << "\nHost PID: " << hostPid << std::endl;
        lab2::client::ClientController controller(logger);
        controller.run(hostPid);
        return EXIT_SUCCESS;
    } catch (const std::exception& error) {
        logger.error(error.what());
        return EXIT_FAILURE;
    }
}
