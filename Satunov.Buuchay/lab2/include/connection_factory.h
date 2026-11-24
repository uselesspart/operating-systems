#pragma once

#include "conn.h"

#include <memory>
#include <sys/types.h>

[[nodiscard]] std::unique_ptr<Conn>
make_connection(pid_t host_pid, bool create);