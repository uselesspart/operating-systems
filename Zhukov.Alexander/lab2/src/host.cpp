#include "app.h"
#include "conn.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>

int main(int argc, char* argv[]) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            std::cout << "Usage: " << argv[0] << '\n';
            return 0;
        }
        if (argc != 1) {
            throw std::runtime_error("Usage: host");
        }
        return run_app(argc, argv, make_connection(), getpid(), true);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
