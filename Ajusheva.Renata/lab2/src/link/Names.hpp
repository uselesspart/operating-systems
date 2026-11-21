#pragma once

#include "link/ConnKey.hpp"

#include <string>

namespace chatlab::names {

inline constexpr const char* kUp = "up";
inline constexpr const char* kDown = "down";

std::string workDir(pid_t host);
std::string fifoPath(const ConnKey& key, const char* way);
std::string socketPath(const ConnKey& key);
std::string semaphorePrefix(pid_t host);
std::string semaphoreName(const ConnKey& key, const char* way);

}
