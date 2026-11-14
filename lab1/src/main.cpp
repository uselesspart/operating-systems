#include "daemon.hpp"

int main(int argc, char** argv) {
    const char* config = (argc > 1) ? argv[1] : "daemon.conf";
    return Daemon::instance().run(config);
}
