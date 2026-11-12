#include "ipc/Names.h"

namespace wolf::names {

std::string runtimeDir(pid_t hostPid) { return "/tmp/wolf_" + std::to_string(hostPid); }

std::string fifoPath(const ConnId& id, const char* direction) {
    return runtimeDir(id.hostPid) + "/fifo_" + std::to_string(id.clientId) + "_" + direction;
}

}
