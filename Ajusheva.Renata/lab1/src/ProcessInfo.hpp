#pragma once

#include <optional>
#include <string>

#include <sys/types.h>


namespace diskmond::process {


bool isAlive(pid_t pid);

std::optional<std::string> name(pid_t pid);


std::optional<std::string> executableName(pid_t pid);


bool isSameProgram(pid_t pid);

}  
