#include "ipc/Names.h"

namespace chat::names {

namespace {

std::string channelSuffix(const ConnId& id, const char* direction)
{
    return std::to_string(id.clientId) + "_" + direction;
}

}

std::string runtimeDir(pid_t hostPid)
{
    return "/tmp/chat_" + std::to_string(hostPid);
}

std::string fifoPath(const ConnId& id, const char* direction)
{
    return runtimeDir(id.hostPid) + "/fifo_" + channelSuffix(id, direction);
}

std::string socketPath(const ConnId& id)
{
    return runtimeDir(id.hostPid) + "/sock_" + std::to_string(id.clientId);
}

std::string semaphorePrefix(pid_t hostPid)
{
    return "/chat_" + std::to_string(hostPid) + "_";
}

std::string semaphoreName(const ConnId& id, const char* direction)
{
    return semaphorePrefix(id.hostPid) + channelSuffix(id, direction);
}

}
