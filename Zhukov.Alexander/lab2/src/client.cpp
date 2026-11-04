#include "arguments.h"
#include "app.h"
#include "conn.h"

#include <iostream>
#include <stdexcept>
#include <string>

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "Usage: " << argv[0] << " <host_pid>\n";
            return 0;
        }
        if (argc != 2) {
            throw std::runtime_error("Usage: client <host_pid>");
        }
        pid_t host_pid = read_pid(argv[1]);
        return run_app(argc, argv, make_connection(), host_pid, false);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
