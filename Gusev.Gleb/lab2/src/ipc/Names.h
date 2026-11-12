#pragma once

#include <string>

#include "ipc/ConnId.h"

// Имена файлов, общих для хоста и его потомков. Привязаны к pid хоста,
// поэтому два запущенных хоста не мешают друг другу.
namespace wolf::names {

inline constexpr const char* kToHost = "up";
inline constexpr const char* kToClient = "down";

std::string runtimeDir(pid_t hostPid);
std::string fifoPath(const ConnId& id, const char* direction);

}
