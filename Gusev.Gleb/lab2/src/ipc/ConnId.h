#pragma once

#include <sys/types.h>

namespace wolf {

// Номер соединения: pid хоста и номер козлёнка. Хост и потомок строят по нему
// одинаковые имена FIFO; для pipe и socketpair номера достаточно как ключа.
struct ConnId {
    pid_t hostPid = 0;
    int clientId = 0;
};

}
