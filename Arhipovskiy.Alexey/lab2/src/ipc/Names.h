#ifndef CHAT_NAMES_H
#define CHAT_NAMES_H

#include "ipc/ConnId.h"

#include <string>

// File system and IPC names derived from (host pid, client id), so both sides compute them independently
namespace chat::names {

inline constexpr const char* kToHost = "up";
inline constexpr const char* kToClient = "down";

std::string runtimeDir(pid_t hostPid);
std::string fifoPath(const ConnId& id, const char* direction);
std::string socketPath(const ConnId& id);
std::string semaphoreName(const ConnId& id, const char* direction);
std::string semaphorePrefix(pid_t hostPid);

}

#endif // CHAT_NAMES_H
