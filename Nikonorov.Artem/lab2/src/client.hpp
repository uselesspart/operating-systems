#pragma once

#include "shared_sync.hpp"

#include <sys/types.h>

#include <string>

int RunClient(const std::string& transport, pid_t host_pid, SharedSync* sync, int inherited_socket = -1);
