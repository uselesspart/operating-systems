#pragma once

#include "conn.h"
#include <sys/types.h>

int run_app(int argc, char* argv[], std::unique_ptr<Conn> conn, pid_t host_pid, bool host);
