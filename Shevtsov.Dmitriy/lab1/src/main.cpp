#include "daemon/daemon.h"

int main(int argc, char* argv[]){
    return Daemon::instance().run(argc > 1 ? argv[1] : "reminder.conf");
}
