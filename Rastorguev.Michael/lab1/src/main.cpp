#include "headers/daemon.hpp"

int main(int argc, char* argv[]) {
    const char* config = (argc > 1) ? argv[1] : "schedule.conf";
    return Daemon::Instance().run(config);
}
