#include "ipc/Signals.h"

#include <pthread.h>
#include <signal.h>

namespace wolf::hostsignals {

void blockHostSignals() {
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGTERM);
    ::pthread_sigmask(SIG_BLOCK, &set, nullptr);
}

}
